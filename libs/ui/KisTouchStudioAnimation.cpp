/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioAnimation.h"

#include <QAbstractListModel>
#include <QAction>
#include <QCache>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <KLocalizedString>

#include <KisPart.h>
#include <KisPlaybackEngine.h>
#include <KisViewManager.h>
#include <KisCanvasAnimationState.h>
#include <animation/KisFrameDisplayProxy.h>
#include <kis_canvas2.h>
#include <kis_command_utils.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_node.h>
#include <kis_node_manager.h>
#include <kis_paint_device.h>
#include <kis_processing_applicator.h>
#include <kis_raster_keyframe_channel.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace
{
enum FrameRole { KeyframeRole = Qt::UserRole, InPlaybackRangeRole };

KisKeyframeChannel *rasterChannel(const KisNodeSP &node)
{
    return node ? node->getKeyframeChannel(KisKeyframeChannel::Raster.id()) : nullptr;
}

bool canEditFrames(const KisNodeSP &node)
{
    return node && node->visible() && !node->userLocked() &&
        (node->inherits("KisPaintLayer") || node->inherits("KisFilterMask") ||
         node->inherits("KisTransparencyMask") || node->inherits("KisSelectionBasedLayer"));
}

class FrameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(57, 44);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state & QStyle::State_Selected;
        const bool keyframe = index.data(KeyframeRole).toBool();
        const bool inRange = index.data(InPlaybackRangeRole).toBool();
        const QRect cell = option.rect.adjusted(1, 1, -3, -1);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(selected ? QColor("#5bafff") : QColor("#41454d"));
        painter->setBrush(selected ? QColor("#26466a") : keyframe ? QColor("#343945") : QColor("#272a31"));
        painter->drawRoundedRect(cell, 7, 7);

        const QImage preview = index.data(Qt::DecorationRole).value<QImage>();
        if (!preview.isNull()) {
            const QRect previewArea = cell.adjusted(3, 2, -3, -15);
            const QPoint topLeft = previewArea.center() - QPoint(preview.width() / 2, preview.height() / 2);
            painter->drawImage(topLeft, preview);
        }

        if (keyframe) {
            const QPoint center(cell.left() + 9, cell.top() + 8);
            QPolygon marker;
            marker << center + QPoint(0, -3) << center + QPoint(3, 0)
                   << center + QPoint(0, 3) << center + QPoint(-3, 0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#74beff"));
            painter->drawPolygon(marker);
        }

        QFont font = option.font;
        font.setPixelSize(11);
        font.setBold(selected);
        painter->setFont(font);
        painter->setPen(selected || inRange ? QColor("#edf2fb") : QColor("#a6aab5"));
        painter->drawText(cell.adjusted(0, 0, 0, -3), Qt::AlignHCenter | Qt::AlignBottom,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }
};
}

class KisTouchStudioFrameModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_count;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return {};
        }

        const int frame = index.row();
        const bool keyframe = m_keys.contains(frame);
        switch (role) {
        case Qt::DisplayRole:
            return QString::number(frame);
        case Qt::AccessibleTextRole:
        case Qt::ToolTipRole:
            return keyframe ? i18n("Keyframe %1", frame) : i18n("Frame %1", frame);
        case KeyframeRole:
            return keyframe;
        case InPlaybackRangeRole:
            return m_playbackRange.contains(frame);
        case Qt::DecorationRole:
            return keyframe ? QVariant::fromValue(thumbnail(frame)) : QVariant();
        default:
            return {};
        }
    }

    void clear()
    {
        beginResetModel();
        m_count = 0;
        m_image = nullptr;
        m_node.clear();
        m_keys.clear();
        m_thumbnails.clear();
        endResetModel();
    }

    void refresh(const KisImageSP &image, const KisNodeSP &node, int currentFrame)
    {
        KisKeyframeChannel *channel = rasterChannel(node);
        const QSet<int> keys = channel ? channel->allKeyframeTimes() : QSet<int>();
        const auto lastKey = std::max_element(keys.cbegin(), keys.cend());
        const KisImageAnimationInterface *animation = image->animationInterface();
        const int lastFrame = std::max({0, currentFrame, animation->documentPlaybackRange().end(),
                                        lastKey == keys.cend() ? 0 : *lastKey});
        const int count = qMin(lastFrame, std::numeric_limits<int>::max() - 1) + 1;
        if (m_count != count) {
            beginResetModel();
        }
        const bool reset = m_count != count;
        m_count = count;
        m_image = image;
        m_node = node;
        m_keys = keys;
        m_playbackRange = animation->activePlaybackRange();
        m_thumbnails.clear();
        if (reset) {
            endResetModel();
        } else {
            Q_EMIT dataChanged(index(0), index(m_count - 1));
        }
    }

    void invalidateThumbnails(const KisTimeSpan &range)
    {
        m_thumbnails.clear();
        if (m_count) {
            const int first = qMax(range.start(), 0);
            const int last = range.isInfinite() ? m_count - 1 : qMin(range.end(), m_count - 1);
            if (first <= last) {
                Q_EMIT dataChanged(index(first), index(last), {Qt::DecorationRole});
            }
        }
    }

private:
    QImage thumbnail(int frame) const
    {
        if (const QImage *cached = m_thumbnails.object(frame)) {
            return *cached;
        }

        KisImageSP image = m_image.toStrongRef();
        if (!image || !image->tryBarrierLock(true)) {
            return {};
        }

        QImage preview;
        if (KisKeyframeChannel *channel = rasterChannel(m_node)) {
            const KisRasterKeyframeSP keyframe = channel->keyframeAt<KisRasterKeyframe>(frame);
            if (keyframe && keyframe->hasContent() && m_node->paintDevice()) {
                KisPaintDeviceSP device = new KisPaintDevice(m_node->paintDevice()->colorSpace());
                keyframe->writeFrameToDevice(device);
                preview = device->createThumbnail(44, 24, Qt::KeepAspectRatio);
            }
        }
        image->unlock();

        m_thumbnails.insert(frame, new QImage(preview));
        return preview;
    }

    int m_count = 0;
    KisImageWSP m_image;
    KisNodeSP m_node;
    QSet<int> m_keys;
    KisTimeSpan m_playbackRange;
    mutable QCache<int, QImage> m_thumbnails {96};
};

KisTouchStudioAnimation::KisTouchStudioAnimation(KisViewManager *viewManager, QWidget *parent)
    : QFrame(parent)
    , m_viewManager(viewManager)
    , m_frames(new KisTouchStudioFrameModel(this))
    , m_timeline(new QListView(this))
    , m_play(new QToolButton(this))
    , m_add(new QToolButton(this))
    , m_options(new QToolButton(this))
    , m_frameNumber(new QLabel(this))
    , m_frameRate(new QLabel(this))
{
    setAttribute(Qt::WA_NoMousePropagation);
    setObjectName("touchStudioAnimation");
    setStyleSheet(QStringLiteral(R"(
        QFrame#touchStudioAnimation {
            background: #202227;
            border: 1px solid #41454d;
            border-radius: 14px;
        }
        QFrame#touchStudioAnimation QLabel { color: #c5cbd6; }
        QFrame#touchStudioAnimation QToolButton {
            color: #eef2fa;
            background: #313640;
            border: 1px solid #414651;
            border-radius: 7px;
            padding: 3px 10px;
        }
        QFrame#touchStudioAnimation QToolButton:hover { background: #404956; }
        QFrame#touchStudioAnimation QToolButton:pressed { background: #26466a; }
        QFrame#touchStudioAnimation QToolButton:disabled { color: #80838b; }
        QFrame#touchStudioAnimation QToolButton::menu-indicator { image: none; }
        QFrame#touchStudioAnimation QListView { background: transparent; border: none; outline: none; }
        QFrame#touchStudioAnimation QScrollBar:horizontal {
            height: 9px;
            border: none;
            background: #202227;
            margin: 1px 0;
        }
        QFrame#touchStudioAnimation QScrollBar::handle:horizontal {
            background: #555e6b;
            border-radius: 3px;
            min-width: 25px;
        }
        QFrame#touchStudioAnimation QScrollBar::add-line:horizontal,
        QFrame#touchStudioAnimation QScrollBar::sub-line:horizontal { width: 0; }
        QFrame#touchStudioAnimation QScrollBar::add-page:horizontal,
        QFrame#touchStudioAnimation QScrollBar::sub-page:horizontal { background: none; }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(9, 6, 9, 4);
    layout->setSpacing(3);
    auto *controls = new QHBoxLayout();
    controls->setSpacing(7);
    layout->addLayout(controls);

    m_play->setMinimumHeight(32);
    m_add->setMinimumHeight(32);
    m_options->setMinimumSize(34, 32);
    m_add->setText(i18n("Add Frame"));
    m_add->setToolTip(i18n("Add a blank keyframe to the active layer"));
    m_options->setText(QStringLiteral("⋯"));
    m_options->setAccessibleName(i18n("Frame options"));
    m_options->setToolTip(i18n("Frame options"));
    m_options->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_options);
    m_duplicate = menu->addAction(i18n("Duplicate Keyframe"), this, [this] { editFrame(FrameEdit::Duplicate); });
    m_remove = menu->addAction(i18n("Remove Keyframe"), this, [this] { editFrame(FrameEdit::Remove); });
    m_options->setMenu(menu);
    controls->addWidget(m_play);
    controls->addWidget(m_frameNumber);
    controls->addStretch();
    controls->addWidget(m_frameRate);
    controls->addWidget(m_add);
    controls->addWidget(m_options);

    m_timeline->setAccessibleName(i18n("Animation frames"));
    m_timeline->setFlow(QListView::LeftToRight);
    m_timeline->setUniformItemSizes(true);
    m_timeline->setSelectionMode(QAbstractItemView::SingleSelection);
    m_timeline->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_timeline->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_timeline->setModel(m_frames);
    m_timeline->setItemDelegate(new FrameDelegate(m_timeline));
    m_timeline->viewport()->installEventFilter(this);
    layout->addWidget(m_timeline, 1);

    connect(m_timeline->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) {
        if (current.isValid()) {
            seek(current.row(), !m_scrubbing);
        }
    });
    connect(m_play, &QToolButton::clicked, this, [this] {
        if (m_state) {
            KisPart::instance()->playbackEngine()->playPause();
        }
    });
    connect(m_add, &QToolButton::clicked, this, [this] { editFrame(FrameEdit::Add); });
    connect(m_viewManager, &KisViewManager::viewChanged, this, &KisTouchStudioAnimation::bindCanvas);
    connect(m_viewManager->nodeManager(), &KisNodeManager::sigNodeActivated,
            this, &KisTouchStudioAnimation::refreshFrames);
    bindCanvas();
}

QSize KisTouchStudioAnimation::sizeHint() const
{
    return QSize(560, 102);
}

bool KisTouchStudioAnimation::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_timeline->viewport()) {
        return QFrame::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        m_scrubbing = mouseEvent->button() == Qt::LeftButton && m_timeline->indexAt(mouseEvent->pos()).isValid();
        break;
    }
    case QEvent::MouseMove:
        if (m_scrubbing) {
            const QModelIndex index = m_timeline->indexAt(static_cast<QMouseEvent *>(event)->pos());
            if (index.isValid()) {
                m_timeline->setCurrentIndex(index);
                return true;
            }
        }
        break;
    case QEvent::MouseButtonRelease:
    case QEvent::UngrabMouse:
        if (m_scrubbing) {
            m_scrubbing = false;
            const QModelIndex index = m_timeline->currentIndex();
            if (index.isValid()) {
                seek(index.row());
            }
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}

void KisTouchStudioAnimation::bindCanvas()
{
    for (const QMetaObject::Connection &connection : m_canvasConnections) {
        disconnect(connection);
    }
    m_canvasConnections.clear();
    m_state = nullptr;
    m_scrubbing = false;

    KisCanvas2 *canvas = m_viewManager->canvasBase();
    const KisImageSP image = m_viewManager->image();
    if (canvas && image) {
        m_state = canvas->animationState();
        KisImageAnimationInterface *animation = image->animationInterface();
        m_canvasConnections << connect(m_state, &KisCanvasAnimationState::sigFrameChanged,
                                       this, &KisTouchStudioAnimation::refreshCurrentFrame)
                            << connect(m_state, &KisCanvasAnimationState::sigPlaybackStateChanged,
                                       this, &KisTouchStudioAnimation::refreshCurrentFrame)
                            << connect(animation, &KisImageAnimationInterface::sigDocumentRangeChanged,
                                       this, &KisTouchStudioAnimation::refreshFrames)
                            << connect(animation, &KisImageAnimationInterface::sigPlaybackRangeChanged,
                                       this, &KisTouchStudioAnimation::refreshFrames)
                            << connect(animation, &KisImageAnimationInterface::sigFramerateChanged,
                                       this, &KisTouchStudioAnimation::refreshCurrentFrame)
                            << connect(animation, &KisImageAnimationInterface::sigKeyframeAdded,
                                       this, &KisTouchStudioAnimation::refreshFrames)
                            << connect(animation, &KisImageAnimationInterface::sigKeyframeRemoved,
                                       this, &KisTouchStudioAnimation::refreshFrames)
                            << connect(animation, &KisImageAnimationInterface::sigFramesChanged, this,
                                       [this](const KisTimeSpan &range) { m_frames->invalidateThumbnails(range); })
                            << connect(image.data(), &KisImage::sigNodeChanged, this, [this](KisNodeSP node) {
                                   if (node == m_viewManager->activeNode()) {
                                       refreshCurrentFrame();
                                   }
                               });
    }
    refreshFrames();
}

void KisTouchStudioAnimation::refreshFrames()
{
    const QSignalBlocker blocker(m_timeline->selectionModel());
    if (m_state) {
        m_frames->refresh(m_viewManager->image(), m_viewManager->activeNode(), m_state->displayProxy()->activeFrame());
    } else {
        m_frames->clear();
    }
    refreshCurrentFrame();
}

void KisTouchStudioAnimation::refreshCurrentFrame()
{
    const KisImageSP image = m_viewManager->image();
    const bool available = m_state && image;
    const bool playing = available && m_state->playbackState() == PLAYING;
    const int frame = available ? m_state->displayProxy()->activeFrame() : 0;
    if (available && frame >= m_frames->rowCount()) {
        refreshFrames();
        return;
    }

    m_play->setText(playing ? i18n("Pause") : i18n("Play"));
    m_play->setEnabled(available);
    m_frameNumber->setText(available ? i18n("Frame %1", frame) : QString());
    m_frameRate->setText(available ? i18n("%1 fps", image->animationInterface()->framerate()) : QString());
    m_timeline->setEnabled(available);
    if (available) {
        const QModelIndex index = m_frames->index(frame);
        const QSignalBlocker blocker(m_timeline->selectionModel());
        m_timeline->setCurrentIndex(index);
        m_timeline->scrollTo(index, QAbstractItemView::EnsureVisible);
        m_timeline->viewport()->update();
    }

    const KisNodeSP node = m_viewManager->activeNode();
    const bool editable = available && !playing && canEditFrames(node);
    KisKeyframeChannel *channel = editable ? rasterChannel(node) : nullptr;
    m_add->setEnabled(editable);
    m_options->setEnabled(editable);
    m_duplicate->setEnabled(editable);
    m_remove->setEnabled(channel && channel->keyframeAt(frame));
}

void KisTouchStudioAnimation::seek(int frame, bool finalize)
{
    if (!m_state) {
        return;
    }
    KisPlaybackEngine *engine = KisPart::instance()->playbackEngine();
    if (m_state->playbackState() == PLAYING) {
        engine->pause();
    }
    engine->seek(frame, finalize ? SEEK_FINALIZE | SEEK_PUSH_AUDIO : SEEK_NONE);
}

void KisTouchStudioAnimation::editFrame(FrameEdit edit)
{
    const KisImageSP image = m_viewManager->image();
    KisNodeSP node = m_viewManager->activeNode();
    if (!image || !m_state || m_state->playbackState() == PLAYING || !canEditFrames(node)) {
        return;
    }

    const int source = m_state->displayProxy()->activeFrame();
    KisKeyframeChannel *channel = rasterChannel(node);
    int target = source;
    if (edit != FrameEdit::Remove) {
        // Editing an unanimated layer creates its original keyframe at time zero.
        target = channel ? source : qMax(1, source);
        while (channel && channel->keyframeAt(target) && target < std::numeric_limits<int>::max() - 1) {
            ++target;
        }
    }

    const auto description = edit == FrameEdit::Remove ? kundo2_i18n("Remove Keyframe")
        : edit == FrameEdit::Duplicate ? kundo2_i18n("Duplicate Keyframe")
        : kundo2_i18n("Add Keyframe");
    auto *command = new KisCommandUtils::LambdaCommand(description, [node, edit, source, target]() mutable -> KUndo2Command * {
        KisKeyframeChannel *channel = rasterChannel(node);
        auto command = std::make_unique<KUndo2Command>();
        if (edit == FrameEdit::Remove) {
            if (!channel || !channel->keyframeAt(target)) {
                return nullptr;
            }
            channel->removeKeyframe(target, command.get());
            return command.release();
        }

        if (!channel) {
            node->enableAnimation();
            channel = node->getKeyframeChannel(KisKeyframeChannel::Raster.id(), true);
        }
        // Enabling animation makes the original artwork the first keyframe.
        if (!channel || channel->keyframeAt(target)) {
            return nullptr;
        }

        const KisKeyframeSP reference = channel->activeKeyframeAt(source);
        if (edit == FrameEdit::Duplicate && reference) {
            channel->copyKeyframe(channel->activeKeyframeTime(source), target, command.get());
        } else {
            channel->addKeyframe(target, command.get());
            if (reference) {
                channel->keyframeAt(target)->setColorLabel(reference->colorLabel());
            }
        }
        return command.release();
    });

    KisProcessingApplicator::runSingleCommandStroke(image, command, KisStrokeJobData::BARRIER,
                                                    KisStrokeJobData::EXCLUSIVE);
    if (edit != FrameEdit::Remove && target != source) {
        seek(target);
    }
}
