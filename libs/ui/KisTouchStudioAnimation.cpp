/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioAnimation.h"

#include <QAbstractListModel>
#include <QAction>
#include <QCache>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

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
#include <commands_new/KisImageAnimSettingCommand.h>
#include <kis_layer_properties_icons.h>
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

QIcon stripIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/light_%1.svg").arg(name));
}

const char popupStyle[] = R"(
    QMenu { background: #25262a; color: #f3f4f6; border: 1px solid #43444a; padding: 4px; }
    QMenu::item { padding: 11px 18px 11px 24px; }
    QMenu::item:selected { background: #34465e; }
    QMenu::item:disabled { color: #9799a0; }
    QMenu::separator { height: 1px; background: #42434a; margin: 4px 8px; }
)";

KisKeyframeChannel *rasterChannel(const KisNodeSP &node)
{
    return node ? node->getKeyframeChannel(KisKeyframeChannel::Raster.id()) : nullptr;
}

bool supportsRasterFrames(const KisNodeSP &node)
{
    return node && (node->inherits("KisPaintLayer") || node->inherits("KisFilterMask") ||
                    node->inherits("KisTransparencyMask") || node->inherits("KisSelectionBasedLayer"));
}

bool canEditFrames(const KisNodeSP &node)
{
    return supportsRasterFrames(node) && node->isEditable();
}

class FrameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(74, 60);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state & QStyle::State_Selected;
        const bool focused = option.state & QStyle::State_HasFocus;
        const bool keyframe = index.data(KeyframeRole).toBool();
        const bool inRange = index.data(InPlaybackRangeRole).toBool();
        const QRect cell = option.rect;
        const QRect ruler = cell.adjusted(0, cell.height() - 18, 0, 0);
        const QColor accent("#87b9fa");

        painter->save();
        painter->setClipRect(cell);
        painter->fillRect(cell, selected ? QColor("#303e51") : QColor("#222327"));
        painter->fillRect(ruler, selected ? QColor("#394b63") : QColor("#292a2e"));
        painter->setPen(QColor("#38393f"));
        painter->drawLine(cell.topRight(), cell.bottomRight());
        painter->drawLine(ruler.topLeft(), ruler.topRight());

        const QImage preview = index.data(Qt::DecorationRole).value<QImage>();
        if (!preview.isNull()) {
            const QRect previewArea = cell.adjusted(5, 2, -5, -19);
            const QPoint origin = previewArea.center() - QPoint((preview.width() - 1) / 2, (preview.height() - 1) / 2);
            const QRect thumbnail(origin, preview.size());
            painter->save();
            painter->setClipRect(previewArea, Qt::IntersectClip);
            painter->setClipRect(thumbnail, Qt::IntersectClip);
            painter->fillRect(thumbnail, QColor("#424348"));
            constexpr int tile = 6;
            for (int y = 0; y < thumbnail.height(); y += tile) {
                for (int x = 0; x < thumbnail.width(); x += tile) {
                    if ((x / tile + y / tile) % 2 == 0) {
                        painter->fillRect(thumbnail.x() + x, thumbnail.y() + y, tile, tile, QColor("#53545a"));
                    }
                }
            }
            painter->drawImage(origin, preview);
            painter->restore();
        }

        if (keyframe) {
            const QPoint center(ruler.left() + 10, ruler.center().y());
            QPolygon marker;
            marker << center + QPoint(0, -3) << center + QPoint(3, 0)
                   << center + QPoint(0, 3) << center + QPoint(-3, 0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? accent : QColor("#babcc3"));
            painter->drawPolygon(marker);
        }

        QFont font = option.font;
        font.setPixelSize(12);
        font.setBold(selected);
        painter->setFont(font);
        painter->setPen(selected ? QColor("#ffffff") : inRange ? QColor("#e2e3e7") : QColor("#a1a2a9"));
        painter->drawText(ruler, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());

        if (selected) {
            painter->fillRect(QRect(cell.x(), cell.y(), cell.width() - 1, 2), accent);
        }
        if (focused) {
            painter->setPen(QPen(QColor("#c5deff"), 1, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cell.adjusted(2, 3, -3, -2));
        }
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
            return keyframe || (frame == 0 && m_keys.isEmpty()) ? QVariant::fromValue(thumbnail(frame)) : QVariant();
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
                preview = device->createThumbnail(64, 39, Qt::KeepAspectRatio);
            }
        } else if (m_node && m_node->paintDevice()) {
            preview = m_node->paintDevice()->createThumbnail(64, 39, Qt::KeepAspectRatio);
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
    , m_previous(new QToolButton(this))
    , m_play(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_track(new QToolButton(this))
    , m_onion(new QToolButton(this))
    , m_add(new QToolButton(this))
    , m_options(new QToolButton(this))
    , m_tracks(new QMenu(m_track))
{
    setAttribute(Qt::WA_NoMousePropagation);
    setObjectName("touchStudioAnimation");
    setStyleSheet(QStringLiteral(R"(
        QFrame#touchStudioAnimation {
            background: #25262a;
            border: 1px solid #42434a;
            border-radius: 12px;
        }
        QFrame#touchStudioAnimation QFrame#touchTransportDivider { background: #42434a; }
        QFrame#touchStudioAnimation QToolButton {
            color: #f3f4f6;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 3px 6px;
        }
        QFrame#touchStudioAnimation QToolButton:hover { background: #34353b; }
        QFrame#touchStudioAnimation QToolButton:pressed,
        QFrame#touchStudioAnimation QToolButton:checked { background: #394b63; }
        QFrame#touchStudioAnimation QToolButton:focus { border-color: #9bc6fa; }
        QFrame#touchStudioAnimation QToolButton:disabled { color: #909197; }
        QFrame#touchStudioAnimation QToolButton::menu-indicator { image: none; }
        QFrame#touchStudioAnimation QListView {
            background: #222327;
            border: none;
            border-top: 1px solid #3b3c43;
            outline: none;
        }
        QFrame#touchStudioAnimation QScrollBar:horizontal {
            height: 9px;
            border: none;
            background: #25262a;
            margin: 0;
        }
        QFrame#touchStudioAnimation QScrollBar::handle:horizontal {
            background: #5d606a;
            border-radius: 2px;
            min-width: 30px;
            margin: 2px 0;
        }
        QFrame#touchStudioAnimation QScrollBar::add-line:horizontal,
        QFrame#touchStudioAnimation QScrollBar::sub-line:horizontal { width: 0; }
        QFrame#touchStudioAnimation QScrollBar::add-page:horizontal,
        QFrame#touchStudioAnimation QScrollBar::sub-page:horizontal { background: none; }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(1);
    auto *controls = new QHBoxLayout();
    controls->setSpacing(3);
    layout->addLayout(controls);

    const auto configureButton = [](QToolButton *button, const QString &icon, const QString &name) {
        button->setMinimumSize(38, 36);
        button->setIconSize(QSize(20, 20));
        button->setIcon(stripIcon(icon));
        button->setAccessibleName(name);
        button->setToolTip(name);
    };
    configureButton(m_previous, QStringLiteral("prevkeyframe"), i18n("Previous keyframe"));
    configureButton(m_play, QStringLiteral("animation_play"), i18n("Play"));
    configureButton(m_next, QStringLiteral("nextkeyframe"), i18n("Next keyframe"));
    configureButton(m_track, QStringLiteral("all-layers"), i18n("Active animation layer"));
    configureButton(m_onion, QStringLiteral("onionOff"), i18n("Onion skin"));
    configureButton(m_add, QStringLiteral("keyframe-add"), i18n("Add a blank keyframe"));
    m_add->setText(i18n("Add Frame"));
    m_add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_options->setMinimumSize(38, 36);
    m_options->setText(QStringLiteral("⋯"));
    m_options->setAccessibleName(i18n("Animation options"));
    m_options->setToolTip(i18n("Animation options"));
    m_options->setPopupMode(QToolButton::InstantPopup);
    m_track->setPopupMode(QToolButton::InstantPopup);
    m_track->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_track->setMenu(m_tracks);
    m_onion->setCheckable(true);
    connect(m_onion, &QToolButton::toggled, this, [this](bool enabled) {
        m_onion->setIcon(stripIcon(enabled ? QStringLiteral("onionOn") : QStringLiteral("onionOff")));
    });
    m_tracks->setStyleSheet(QString::fromLatin1(popupStyle));
    connect(m_tracks, &QMenu::aboutToShow, this, &KisTouchStudioAnimation::populateTracks);

    auto *menu = new QMenu(m_options);
    menu->setStyleSheet(QString::fromLatin1(popupStyle));
    m_duplicate = menu->addAction(i18n("Duplicate Keyframe"), this, [this] { editFrame(FrameEdit::Duplicate); });
    m_remove = menu->addAction(i18n("Remove Keyframe"), this, [this] { editFrame(FrameEdit::Remove); });
    menu->addSeparator();
    m_playbackSettings = menu->addAction(i18n("Playback Settings…"), this, &KisTouchStudioAnimation::editPlaybackSettings);
    m_options->setMenu(menu);

    controls->addWidget(m_previous);
    controls->addWidget(m_play);
    controls->addWidget(m_next);
    auto *transportDivider = new QFrame(this);
    transportDivider->setObjectName("touchTransportDivider");
    transportDivider->setFixedSize(1, 19);
    controls->addWidget(transportDivider);
    controls->addWidget(m_track);
    controls->addStretch();
    controls->addWidget(m_onion);
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
    connect(m_previous, &QToolButton::clicked, this, [] { KisPart::instance()->playbackEngine()->previousKeyframe(); });
    connect(m_next, &QToolButton::clicked, this, [] { KisPart::instance()->playbackEngine()->nextKeyframe(); });
    connect(m_onion, &QToolButton::clicked, this, &KisTouchStudioAnimation::toggleOnionSkin);
    connect(m_add, &QToolButton::clicked, this, [this] { editFrame(FrameEdit::Add); });
    connect(m_viewManager, &KisViewManager::viewChanged, this, &KisTouchStudioAnimation::bindCanvas);
    connect(m_viewManager->nodeManager(), &KisNodeManager::sigNodeActivated,
            this, &KisTouchStudioAnimation::refreshFrames);
    bindCanvas();
}

QSize KisTouchStudioAnimation::sizeHint() const
{
    return QSize(760, 114);
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
    case QEvent::Wheel: {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->angleDelta().x() == 0 && wheel->pixelDelta().x() == 0) {
            QScrollBar *scrollBar = m_timeline->horizontalScrollBar();
            const int delta = wheel->pixelDelta().isNull() ? wheel->angleDelta().y() / 2 : wheel->pixelDelta().y();
            scrollBar->setValue(scrollBar->value() - delta);
            return true;
        }
        break;
    }
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

    const QString playLabel = playing ? i18n("Pause") : i18n("Play");
    if (m_play->accessibleName() != playLabel) {
        m_play->setIcon(stripIcon(playing ? QStringLiteral("animation_pause") : QStringLiteral("animation_play")));
        m_play->setToolTip(playLabel);
        m_play->setAccessibleName(playLabel);
    }
    m_play->setEnabled(available);
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
    KisKeyframeChannel *channel = available ? rasterChannel(node) : nullptr;
    m_previous->setEnabled(channel && frame > channel->firstKeyframeTime());
    m_next->setEnabled(channel && frame < channel->lastKeyframeTime());
    m_track->setEnabled(available);
    updateTrackLabel();
    const QVariant onion = node ? KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::onionSkins, QVariant()) : QVariant();
    m_onion->setEnabled(available && !playing && onion.isValid());
    m_onion->setChecked(onion.toBool());
    m_add->setEnabled(editable);
    m_options->setEnabled(available);
    m_playbackSettings->setEnabled(available && !playing);
    if (available) {
        m_playbackSettings->setText(i18n("Playback Settings · %1 fps…", image->animationInterface()->framerate()));
    }
    m_duplicate->setEnabled(editable);
    m_remove->setEnabled(editable && channel && channel->keyframeAt(frame));
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
    auto *command = new KisCommandUtils::LambdaCommand(description, [node, edit, source, target, owner = image.data()]() mutable -> KUndo2Command * {
        if (node->graphListener() != owner || !canEditFrames(node)) {
            return nullptr;
        }
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

void KisTouchStudioAnimation::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    m_add->setToolButtonStyle(width() < 460 ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    updateTrackLabel();
}

void KisTouchStudioAnimation::updateTrackLabel()
{
    const KisNodeSP node = m_viewManager->activeNode();
    const QString fullName = node ? node->name() : i18n("Layers");
    const int labelWidth = qBound(0, width() - (width() < 460 ? 300 : 360), 160);
    m_track->setToolButtonStyle(labelWidth >= 25 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    m_track->setText(m_track->fontMetrics().elidedText(fullName, Qt::ElideRight, labelWidth));
    m_track->setToolTip(i18n("Active animation layer: %1", fullName));
}

void KisTouchStudioAnimation::populateTracks()
{
    m_tracks->clear();
    const KisImageSP image = m_viewManager->image();
    if (!image) {
        return;
    }
    const KisNodeSP active = m_viewManager->activeNode();
    const auto addTracks = [this, &active](auto &&self, const KisNodeSP &parent, int depth) -> void {
        for (KisNodeSP node = parent->lastChild(); node; node = node->prevSibling()) {
            if (supportsRasterFrames(node) || rasterChannel(node)) {
                const QString name = QString(qMin(depth, 6) * 2, QLatin1Char(' ')) + node->name();
                QAction *select = m_tracks->addAction(name);
                select->setCheckable(true);
                select->setChecked(node == active);
                connect(select, &QAction::triggered, this, [this, track = KisNodeWSP(node)] {
                    const KisNodeSP target = track.toStrongRef();
                    const KisImageSP currentImage = m_viewManager->image();
                    if (target && currentImage && target->graphListener() == currentImage.data()) {
                        auto *manager = m_viewManager->nodeManager();
                        manager->slotNonUiActivatedNode(target);
                        manager->slotSetSelectedNodes(KisNodeList {target});
                    }
                });
            }
            self(self, node, depth + 1);
        }
    };
    addTracks(addTracks, image->root(), 0);
    if (m_tracks->isEmpty()) {
        m_tracks->addAction(i18n("No animation layers"))->setEnabled(false);
    }
}

void KisTouchStudioAnimation::toggleOnionSkin()
{
    const KisImageSP image = m_viewManager->image();
    const KisNodeSP node = m_viewManager->activeNode();
    if (image && node) {
        KisLayerPropertiesIcons::setNodePropertyAutoUndo(node, KisLayerPropertiesIcons::onionSkins,
                                                        m_onion->isChecked(), image);
    }
}

void KisTouchStudioAnimation::editPlaybackSettings()
{
    const KisImageSP image = m_viewManager->image();
    if (!image) {
        return;
    }
    KisImageAnimationInterface *animation = image->animationInterface();
    const KisTimeSpan range = animation->documentPlaybackRange();
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n("Playback Settings"));
    auto *layout = new QFormLayout(dialog);

    auto *fps = new QSpinBox(dialog);
    fps->setRange(1, qMax(240, animation->framerate()));
    fps->setValue(animation->framerate());
    auto *start = new QSpinBox(dialog);
    auto *end = new QSpinBox(dialog);
    const int lastFrame = qMax(range.end(), 9999);
    start->setRange(0, lastFrame);
    end->setRange(0, lastFrame);
    start->setValue(range.start());
    end->setValue(range.end());
    for (auto *field : {fps, start, end}) {
        field->setMinimumHeight(36);
    }

    layout->addRow(i18n("Frames per second"), fps);
    layout->addRow(i18n("Start frame"), start);
    layout->addRow(i18n("End frame"), end);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(start, qOverload<int>(&QSpinBox::valueChanged), end, [end](int value) {
        if (end->value() < value) {
            end->setValue(value);
        }
    });
    connect(end, qOverload<int>(&QSpinBox::valueChanged), start, [start](int value) {
        if (start->value() > value) {
            start->setValue(value);
        }
    });
    connect(dialog, &QDialog::accepted, this, [this, source = KisImageWSP(image), fps, start, end] {
        const KisImageSP image = source.toStrongRef();
        if (!image || image != m_viewManager->image().toStrongRef()) {
            return;
        }
        const KisImageAnimSettingCommand::Settings settings {fps->value(), start->value(), end->value()};
        KisImageAnimationInterface *animation = image->animationInterface();
        const KisTimeSpan range = animation->documentPlaybackRange();
        if (settings.FPS != animation->framerate() || settings.startFrame != range.start() || settings.endFrame != range.end()) {
            KisProcessingApplicator::runSingleCommandStroke(image, new KisImageAnimSettingCommand(animation, settings),
                                                            KisStrokeJobData::BARRIER, KisStrokeJobData::EXCLUSIVE);
        }
    });
    dialog->open();
}
