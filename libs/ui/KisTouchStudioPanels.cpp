/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioPanels.h"
#include "KisTouchStudioDockers.h"

#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScroller>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KisColorSelectorConfiguration.h>
#include <KoCanvasResourceProvider.h>
#include <KisResourceModel.h>
#include <KisResourceItemChooser.h>
#include <KisResourceItemListView.h>
#include <KisResourceThumbnailCache.h>
#include <KisResourceTypes.h>
#include <KisTagFilterResourceProxyModel.h>
#include <KisTagModel.h>
#include <KisUniqueColorSet.h>
#include <KisVisualColorSelector.h>
#include <KoColorDisplayRendererInterface.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOp.h>
#include <KoCompositeOpRegistry.h>
#include <KoToolBase.h>
#include <KoToolManager.h>
#include <kactioncollection.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer_properties_icons.h>
#include <kis_node.h>
#include <kis_paint_device.h>
#include <kis_paintop_factory.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_registry.h>

#include "KisDocument.h"
#include "KisBrushStrokePreviewCache.h"
#include "KisViewManager.h"
#include "kis_action.h"
#include "kis_action_manager.h"
#include "kis_canvas2.h"
#include "kis_canvas_resource_provider.h"
#include "kis_cmb_composite.h"
#include "kis_node_manager.h"
#include "kis_node_model.h"
#include "kis_paintop_box.h"
#include "kis_preset_chooser.h"
#include "kis_shape_controller.h"
#include "kis_smoothing_options.h"

namespace
{
constexpr const char *smoothingActions[] = {
    "set_no_brush_smoothing",
    "set_simple_brush_smoothing",
    "set_weighted_brush_smoothing",
    "set_stabilizer_brush_smoothing",
    "set_pixel_perfect_smoothing",
};

class TouchPercentSlider final : public QSlider
{
public:
    explicit TouchPercentSlider(QWidget *parent)
        : QSlider(Qt::Horizontal, parent)
    {
        setTracking(false);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setSliderDown(true);
        moveTo(event->pos().x());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown()) {
            moveTo(event->pos().x());
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->pos().x());
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void moveTo(int x)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const int position = x - groove.x() - handle.width() / 2;
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(), maximum(), position,
                                                          groove.width() - handle.width(), option.upsideDown));
    }
};

void drawRowSurface(QPainter *painter, const QStyleOptionViewItem &option)
{
    const QRect row = option.rect.adjusted(1, 1, -1, -1);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    if (selected || hovered || (option.state & QStyle::State_HasFocus)) {
        painter->setPen(option.state & QStyle::State_HasFocus ? QPen(QColor("#9ac6ff")) : QPen(Qt::NoPen));
        painter->setBrush(selected ? QColor("#395f88") : hovered ? QColor("#343538") : Qt::transparent);
        painter->drawRoundedRect(row, 5, 5);
    } else {
        painter->setPen(QColor("#37383c"));
        painter->drawLine(row.bottomLeft() + QPoint(8, 0), row.bottomRight() - QPoint(8, 0));
    }
}

void drawListEntry(QPainter *painter, const QStyleOptionViewItem &option, const QImage &thumbnail,
                   const QIcon &fallback, const QString &title, const QString &detail)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect row = option.rect.adjusted(3, 2, -3, -2);
    drawRowSurface(painter, option);

    const QRect tile = QStyle::visualRect(option.direction, row, QRect(row.left() + 8, row.center().y() - 21, 44, 44));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor("#191a1c"));
    painter->drawRoundedRect(tile, 3, 3);
    if (thumbnail.isNull()) {
        fallback.paint(painter, tile.adjusted(6, 6, -6, -6));
    } else {
        const QSize fitted = thumbnail.size().scaled(tile.size() - QSize(4, 4), Qt::KeepAspectRatio);
        const QRect imageRect(tile.center().x() - fitted.width() / 2, tile.center().y() - fitted.height() / 2,
                              fitted.width(), fitted.height());
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(imageRect, thumbnail);
    }

    const QRect textRect = QStyle::visualRect(option.direction, row, row.adjusted(62, 0, -10, 0));
    QFont titleFont = option.font;
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(QColor("#e5ebf5"));
    QRect titleRect = textRect;
    if (!detail.isEmpty()) {
        titleRect.setBottom(textRect.center().y() + 1);
    }
    painter->drawText(titleRect, Qt::AlignLeading | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(title, Qt::ElideRight, textRect.width()));

    if (!detail.isEmpty()) {
        QRect detailRect = textRect;
        detailRect.setTop(textRect.center().y() + 1);
        painter->setPen(QColor("#b2bfd1"));
        painter->setFont(option.font);
        painter->drawText(detailRect, Qt::AlignLeading | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(detail, Qt::ElideRight, textRect.width()));
    }
    painter->restore();
}

class TouchPresetDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(0, 84);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const qreal scale = painter->device()->devicePixelRatioF();
        QString name = index.data(Qt::UserRole + KisAbstractResourceModel::Name).toString();
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        const auto metadata = index.data(Qt::UserRole + KisAbstractResourceModel::MetaData).toMap();
        const QString engine = metadata.value(QStringLiteral("paintopid")).toString();
        const auto *factory = KisPaintOpRegistry::instance()->value(engine);
        const QRect row = option.rect.adjusted(3, 2, -3, -2);
        const int presetId = index.data(Qt::UserRole + KisAbstractResourceModel::Id).toInt();
        if (row.width() < 80 || presetId < 0 || KisBrushStrokePreviewCache::isNoPreviewEngine(engine)) {
            const auto size = QSize(44, 44) * scale;
            const QImage thumbnail = KisResourceThumbnailCache::instance()->getImage(
                index, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            drawListEntry(painter, option, thumbnail, QIcon(), name, factory ? factory->name() : QString());
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        drawRowSurface(painter, option);
        const int captionHeight = qMax(20, option.fontMetrics.height() + 2);
        const QRect preview = row.adjusted(9, captionHeight + 4, -9, -4);
        const QImage stroke = KisBrushStrokePreviewCache::instance()->getPreview(presetId, preview.size() * scale, scale);
        painter->drawImage(preview, stroke);
        painter->setPen(QColor("#efeff1"));
        QFont font = option.font;
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        const QRect title(row.x() + 9, row.y() + 3, row.width() - 18, captionHeight);
        painter->drawText(title, Qt::AlignLeading | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(name, Qt::ElideRight, title.width()));
        painter->restore();
    }
};

class TouchLayerDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(0, 58);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int pixelSize = qRound(44 * painter->device()->devicePixelRatioF());
        const QImage thumbnail = index.data(KisNodeModel::BeginThumbnailRole + pixelSize).value<QImage>();
        const KisNodeSP node = static_cast<const KisNodeModel *>(index.model())->nodeFromIndex(index);
        QStringList details;
        if (node) {
            const auto *group = qobject_cast<const KisGroupLayer *>(node.data());
            if (group && group->passThroughMode()) {
                details.append(i18n("Pass Through"));
            } else if (node->compositeOp() && node->compositeOpId() != COMPOSITE_OVER) {
                details.append(node->compositeOp()->description());
            }
            if (node->opacity() != 255) {
                details.append(i18n("%1%", qRound(node->opacity() * 100.0 / 255)));
            }
            if (!node->visible(true)) {
                details.append(i18n("Hidden"));
            }
            if (KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::locked, false).toBool()) {
                details.append(i18n("Locked"));
            }
        }
        drawListEntry(painter, option, thumbnail, index.data(Qt::DecorationRole).value<QIcon>(),
                      index.data().toString(), details.join(QStringLiteral(" · ")));
    }
};

QString colorSwatchStyle(const QColor &color)
{
    return QStringLiteral(R"(
        QPushButton { background: %1; border: 1px solid #565960; border-radius: 7px; padding: 0; }
        QPushButton:hover, QPushButton:focus { border: 3px solid #81a7d4; }
        QPushButton:pressed { border: 3px solid #395f88; }
    )").arg(color.name());
}

QVBoxLayout *pageLayout(QWidget *page, const QString &title, QWidget *action = nullptr)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto *heading = new QLabel(title, page);
    heading->setObjectName("touchPanelHeading");
    if (action) {
        auto *header = new QHBoxLayout;
        header->addWidget(heading, 1);
        header->addWidget(action);
        layout->addLayout(header);
    } else {
        layout->addWidget(heading);
    }
    return layout;
}
}

KisTouchStudioPanels::KisTouchStudioPanels(KisViewManager *viewManager, QWidget *parent)
    : QWidget(parent)
    , m_viewManager(viewManager)
{
    setObjectName("touchStudioPanels");
    setAttribute(Qt::WA_NoMousePropagation);
    setAttribute(Qt::WA_StyledBackground);
    QPalette panelPalette = palette();
    panelPalette.setColor(QPalette::Window, QColor("#25262a"));
    panelPalette.setColor(QPalette::WindowText, QColor("#efeff1"));
    panelPalette.setColor(QPalette::Base, QColor("#1e1f22"));
    panelPalette.setColor(QPalette::Text, QColor("#efeff1"));
    panelPalette.setColor(QPalette::Button, QColor("#35363a"));
    panelPalette.setColor(QPalette::ButtonText, QColor("#efeff1"));
    panelPalette.setColor(QPalette::Mid, QColor("#45464a"));
    panelPalette.setColor(QPalette::Highlight, QColor("#88baff"));
    panelPalette.setColor(QPalette::HighlightedText, QColor("#111d2e"));
    setPalette(panelPalette);
    setStyleSheet(QStringLiteral(R"(
        #touchStudioPanels {
            background: #25262a;
            border: 1px solid #45464a;
            border-radius: 14px;
        }
        #touchPanelPage QLabel, #touchPanelPage QAbstractItemView {
            color: #efeff1;
        }
        #touchPanelPage #touchPanelHeading {
            font-size: 20px;
            font-weight: bold;
        }
        #touchPanelPage QLineEdit, #touchPanelPage QSpinBox, #touchPanelPage QComboBox {
            color: #efeff1;
            background: #1e1f22;
            border: 1px solid #47484d;
            border-radius: 7px;
            min-height: 38px;
            padding: 0 10px;
        }
        #touchPanelPage QLineEdit:focus, #touchPanelPage QSpinBox:focus, #touchPanelPage QComboBox:focus {
            border-color: #88baff;
        }
        #touchPanelPage QSpinBox QLineEdit {
            background: transparent;
            border: 0;
            min-height: 0;
            padding: 0;
        }
        #touchPanelPage QComboBox:disabled {
            color: #a0a1a6;
        }
        #touchPanelPage QComboBox QAbstractItemView::item {
            min-height: 34px;
        }
        #touchPanelPage QTabBar::tab {
            background: #25262a;
            border: none;
            border-radius: 8px;
            color: #c3c4c8;
            min-height: 44px;
            margin: 0 1px;
            padding: 0 14px;
        }
        #touchPanelPage QTabBar::tab:selected {
            background: #393b40;
            color: #ffffff;
        }
        #touchPanelPage QTabBar::tab:hover {
            color: white;
        }
        #touchPanelPage QAbstractScrollArea {
            background: #1e1f22;
            border: 0;
            border-radius: 8px;
        }
        #touchPanelPage QAbstractItemView {
            selection-background-color: #395f88;
            selection-color: #ffffff;
        }
        #touchPanelPage #touchPresetView {
            background: #25262a;
            selection-background-color: #395f88;
            selection-color: white;
        }
        #touchPanelPage #touchBrushSettings, #touchPanelPage #touchLayerView { background: #25262a; }
        #touchPanelPage QListView#touchBrushTags:focus,
        #touchPanelPage QTabBar:focus { border-bottom: 1px solid #88baff; }
        #touchPanelPage QListView#touchBrushTags { background: transparent; outline: 0; }
        #touchPanelPage QListView#touchBrushTags::item {
            min-height: 40px; padding: 0 6px; margin: 1px 0; border-radius: 7px;
            color: #c4c5c8;
        }
        #touchPanelPage QListView#touchBrushTags::item:selected { background: #3c3e43; color: white; }
        #touchPanelPage QPushButton {
            background: #35363a;
            border: 1px solid transparent;
            border-radius: 6px;
            color: #efeff1;
            min-height: 40px;
            padding: 0 8px;
        }
        #touchPanelPage QPushButton:hover, #touchPanelPage QPushButton:focus {
            border-color: #88baff;
        }
        #touchPanelPage QPushButton:pressed {
            background: #3b6190;
        }
        #touchPanelPage QPushButton:disabled {
            color: #a0a1a6;
        }
        #touchPanelPage #touchAddLayer {
            background: #3b4149;
            color: #efeff1;
            font-weight: 600;
        }
        #touchPanelPage #touchAddLayer:disabled {
            background: #35363a;
            color: #a0a1a6;
        }
        #touchPanelPage #touchLayerMenuButton {
            min-width: 42px;
            padding: 0;
        }
        #touchPanelPage #touchLayerMenuButton::menu-indicator {
            image: none;
        }
        #touchPanelPage QSlider::groove:horizontal {
            background: #14171d;
            border: 1px solid #47484d;
            height: 8px;
            border-radius: 5px;
        }
        #touchPanelPage QSlider::handle:horizontal {
            background: #88baff;
            border: 1px solid #c6e0ff;
            border-radius: 11px;
            width: 22px;
            margin: -7px 0;
        }
        #touchPanelPage QSlider::handle:horizontal:focus {
            background: #b9d9ff;
        }
        #touchPanelPage QSlider::handle:horizontal:disabled {
            background: #5d5e63;
            border-color: #6e7a8d;
        }
        #touchPanelPage QScrollBar:vertical {
            background: transparent;
            width: 8px;
        }
        #touchPanelPage QScrollBar::handle:vertical {
            background: #515256;
            border-radius: 3px;
            min-height: 32px;
            margin: 0 1px;
        }
        #touchPanelPage QScrollBar::add-line:vertical,
        #touchPanelPage QScrollBar::sub-line:vertical {
            height: 0;
        }
        #touchPanelPage QScrollBar::add-page:vertical,
        #touchPanelPage QScrollBar::sub-page:vertical {
            background: transparent;
        }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    auto *brushPage = new QWidget(m_pages);
    brushPage->setObjectName("touchPanelPage");
    m_pages->addWidget(brushPage);
    auto *brushTabs = new QTabBar(brushPage);
    brushTabs->setDrawBase(false);
    brushTabs->setAccessibleName(i18n("Brush panels"));
    brushTabs->addTab(i18n("Library"));
    brushTabs->addTab(i18n("Settings"));
    auto *brushPageLayout = pageLayout(brushPage, i18n("Brushes"), brushTabs);
    auto *brushPages = new QStackedWidget(brushPage);
    brushPageLayout->addWidget(brushPages, 1);
    auto *brushLibrary = new QWidget(brushPages);
    auto *brushLayout = new QVBoxLayout(brushLibrary);
    brushLayout->setContentsMargins(0, 0, 0, 0);
    auto *search = new QLineEdit(brushLibrary);
    search->setPlaceholderText(i18n("Search brush presets"));
    search->setAccessibleName(i18n("Search brush presets"));
    search->setClearButtonEnabled(true);
    brushLayout->addWidget(search);

    auto *tags = new KisTagModel(ResourceType::PaintOpPresets, brushLibrary);
    m_compactTags = new QComboBox(brushLibrary);
    m_compactTags->setAccessibleName(i18n("Brush category"));
    m_compactTags->setModel(tags);
    m_compactTags->setModelColumn(KisAllTagsModel::Name);
    brushLayout->addWidget(m_compactTags);

    auto *catalog = new QHBoxLayout;
    catalog->setSpacing(6);
    brushLayout->addLayout(catalog, 1);
    m_brushTags = new QListView(brushLibrary);
    m_brushTags->setObjectName(QStringLiteral("touchBrushTags"));
    m_brushTags->setAccessibleName(i18n("Brush categories"));
    m_brushTags->setModel(tags);
    m_brushTags->setModelColumn(KisAllTagsModel::Name);
    m_brushTags->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_brushTags->setFixedWidth(108);
    m_brushTags->setGridSize(QSize(96, 44));
    m_brushTags->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_brushTags->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(m_brushTags->viewport(), QScroller::TouchGesture);
    catalog->addWidget(m_brushTags);

    m_presets = new KisPresetChooser(brushLibrary);
    auto *chooser = m_presets->itemChooser();
    chooser->setSynced(false);
    chooser->showStorageBtn(false);
    chooser->setListViewMode(ListViewMode::Detail);
    chooser->setRowHeight(84);
    auto *presetView = chooser->itemView();
    presetView->setObjectName("touchPresetView");
    presetView->setAccessibleName(i18n("Brush presets"));
    presetView->setItemDelegate(new TouchPresetDelegate(presetView));
    connect(KisBrushStrokePreviewCache::instance(), &KisBrushStrokePreviewCache::sigPreviewReady,
            presetView, [presetView] { presetView->viewport()->update(); });
    presetView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    presetView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *results = new QStackedWidget(brushLibrary);
    results->addWidget(m_presets);
    auto *noPresets = new QLabel(i18n("No matching brushes"), results);
    noPresets->setAlignment(Qt::AlignCenter);
    noPresets->setWordWrap(true);
    results->addWidget(noPresets);
    catalog->addWidget(results, 1);
    auto *presetFilter = chooser->tagFilterModel();
    const auto syncResults = [results, presetView] {
        results->setCurrentIndex(presetView->model()->rowCount() > 0 ? 0 : 1);
    };
    connect(presetView->model(), &QAbstractItemModel::modelReset, results, syncResults);
    connect(presetView->model(), &QAbstractItemModel::rowsInserted, results, syncResults);
    connect(presetView->model(), &QAbstractItemModel::rowsRemoved, results, syncResults);
    syncResults();
    presetFilter->setFilterInCurrentTag(true);
    connect(search, &QLineEdit::textChanged, presetFilter, &KisTagFilterResourceProxyModel::setSearchText);
    const auto selectCategory = [this, tags, presetFilter](int row) {
        const auto index = tags->index(row, KisAllTagsModel::Name);
        m_brushTags->setCurrentIndex(index);
        presetFilter->setTagFilter(tags->tagForIndex(index));
    };
    connect(m_compactTags, qOverload<int>(&QComboBox::currentIndexChanged), this, selectCategory);
    connect(m_brushTags->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &index) { m_compactTags->setCurrentIndex(index.row()); });
    selectCategory(m_compactTags->currentIndex());
    brushPages->addWidget(brushLibrary);
    brushPages->addWidget(createBrushSettings(brushPages));
    connect(brushTabs, &QTabBar::currentChanged, brushPages, &QStackedWidget::setCurrentIndex);

    auto *colorPage = new QWidget(m_pages);
    colorPage->setObjectName("touchPanelPage");
    m_pages->addWidget(colorPage);
    auto *colorLayout = pageLayout(colorPage, i18n("Color"));
    colorLayout->setSpacing(12);
    colorPage->setStyleSheet(QStringLiteral(R"(
        QWidget#touchColorContents, QWidget#touchColorViewport, QScrollArea#touchColorScroll {
            background: #25262a;
            border: 0;
        }
        QWidget#touchColorContents QLabel {
            color: #efeff1;
        }
        QWidget#touchColorContents QLabel[touchColorCaption="true"] {
            color: #c4c5cb;
            font-weight: 600;
        }
        QWidget#touchColorContents QLineEdit {
            background: #1c1d20;
            color: #efeff1;
            border: 1px solid #47494e;
            border-radius: 7px;
            min-height: 42px;
            padding: 0 9px;
            selection-background-color: #395f88;
            selection-color: #ffffff;
        }
        QWidget#touchColorContents QLineEdit:focus {
            border-color: #81a7d4;
        }
    )"));
    auto *colorScroll = new QScrollArea(colorPage);
    colorScroll->setObjectName("touchColorScroll");
    colorScroll->viewport()->setObjectName("touchColorViewport");
    colorScroll->setWidgetResizable(true);
    colorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(colorScroll->viewport(), QScroller::TouchGesture);
    colorLayout->addWidget(colorScroll, 1);

    auto *colorControls = new QWidget(colorScroll);
    colorControls->setObjectName("touchColorContents");
    colorControls->setAttribute(Qt::WA_StyledBackground);
    colorScroll->setWidget(colorControls);
    auto *colorControlsLayout = new QVBoxLayout(colorControls);
    colorControlsLayout->setContentsMargins(0, 0, 0, 4);
    colorControlsLayout->setSpacing(14);
    const auto addCaption = [](QBoxLayout *layout, QWidget *parent, const QString &text) {
        auto *label = new QLabel(text, parent);
        label->setProperty("touchColorCaption", true);
        layout->addWidget(label);
        return label;
    };

    m_colorSelector = new KisVisualColorSelector(colorControls);
    const KisColorSelectorConfiguration colorConfiguration(
        KisColorSelectorConfiguration::Square, KisColorSelectorConfiguration::Slider,
        KisColorSelectorConfiguration::SV, KisColorSelectorConfiguration::H);
    m_colorSelector->setConfiguration(&colorConfiguration);
    m_colorSelector->setSliderPosition(Qt::TopEdge);
    m_colorSelector->setMinimumSliderWidth(44);
    m_colorSelector->setMinimumHeight(226);
    m_colorSelector->setMaximumHeight(300);
    m_colorSelector->setAcceptTabletEvents(true);
    m_colorSelector->setAccessibleName(i18n("Foreground color selector"));
    colorControlsLayout->addWidget(m_colorSelector, 1);

    auto *colorReadout = new QHBoxLayout;
    colorReadout->setSpacing(10);
    m_colorSample = new QLabel(colorControls);
    m_colorSample->setFixedSize(52, 44);
    auto *hexFields = new QVBoxLayout;
    hexFields->setSpacing(4);
    auto *hexLabel = addCaption(hexFields, colorControls, i18n("Hex"));
    m_colorValue = new QLineEdit(colorControls);
    m_colorValue->setObjectName(QStringLiteral("touchColorValue"));
    m_colorValue->setMaxLength(7);
    m_colorValue->setInputMethodHints(Qt::ImhNoPredictiveText | Qt::ImhPreferUppercase);
    m_colorValue->setAccessibleName(i18n("Hexadecimal RGB color"));
    hexLabel->setBuddy(m_colorValue);
    hexFields->addWidget(m_colorValue);
    colorReadout->addWidget(m_colorSample, 0, Qt::AlignBottom);
    colorReadout->addLayout(hexFields, 1);
    colorControlsLayout->addLayout(colorReadout);

    const auto applyRgb = [this](const QColor &color) {
        KoColor foreground(color, KoColorSpaceRegistry::instance()->rgb8());
        if (m_colorIsGray && m_viewManager->image()) {
            foreground.convertTo(m_viewManager->image()->colorSpace());
        }
        m_viewManager->canvasResourceProvider()->setFGColor(foreground);
    };
    connect(m_colorValue, &QLineEdit::editingFinished, this, [this, applyRgb] {
        static const QRegularExpression rgbHex(QStringLiteral("^#?[0-9a-fA-F]{6}$"));
        const QString hex = m_colorValue->text();
        if (m_colorValue->isModified() && rgbHex.match(hex).hasMatch()) {
            applyRgb(QColor(hex.startsWith(QLatin1Char('#')) ? hex : QStringLiteral("#") + hex));
        }
        syncColor(m_viewManager->canvasResourceProvider()->fgColor());
    });

    m_colorRgbFields = new QWidget(colorControls);
    auto *channels = new QHBoxLayout(m_colorRgbFields);
    channels->setContentsMargins(0, 0, 0, 0);
    channels->setSpacing(10);
    const QString channelNames[] = {i18n("Red"), i18n("Green"), i18n("Blue")};
    for (int channel = 0; channel < 3; ++channel) {
        auto *field = new QVBoxLayout;
        field->setSpacing(4);
        auto *label = addCaption(field, m_colorRgbFields, channelNames[channel]);
        auto *entry = new QLineEdit(m_colorRgbFields);
        entry->setAlignment(Qt::AlignHCenter);
        entry->setMaxLength(3);
        entry->setInputMethodHints(Qt::ImhDigitsOnly);
        entry->setAccessibleName(i18nc("@info:accessibility", "%1, 0 to 255", channelNames[channel]));
        label->setBuddy(entry);
        m_colorChannels[channel] = entry;
        field->addWidget(entry);
        channels->addLayout(field, 1);
        connect(entry, &QLineEdit::editingFinished, this, [this, entry, applyRgb] {
            bool valid = false;
            const int edited = entry->text().toInt(&valid);
            if (entry->isModified() && valid && edited >= 0 && edited <= 255) {
                applyRgb(QColor(m_colorChannels[0]->text().toInt(), m_colorChannels[1]->text().toInt(),
                                m_colorChannels[2]->text().toInt()));
            }
            syncColor(m_viewManager->canvasResourceProvider()->fgColor());
        });
    }
    colorControlsLayout->addWidget(m_colorRgbFields);

    m_colorHistory = new QWidget(colorControls);
    auto *historyGroup = new QVBoxLayout(m_colorHistory);
    historyGroup->setContentsMargins(0, 0, 0, 0);
    historyGroup->setSpacing(6);
    addCaption(historyGroup, m_colorHistory, i18n("Recent Colors"));
    auto *history = new QGridLayout;
    history->setSpacing(8);
    for (int index = 0; index < 6; ++index) {
        auto *button = new QPushButton(m_colorHistory);
        button->setMinimumHeight(44);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->hide();
        m_historyButtons[index] = button;
        history->setColumnStretch(index, 1);
        history->addWidget(button, 0, index);
        connect(button, &QPushButton::clicked, this, [this, index] {
            auto *provider = m_viewManager->canvasResourceProvider();
            auto *recentColors = provider->colorHistoryModel();
            if (index < recentColors->size()) {
                KoColor color = recentColors->color(index);
                if (m_colorIsGray && m_viewManager->image()) {
                    color.convertTo(m_viewManager->image()->colorSpace());
                }
                provider->setFGColor(color);
            }
        });
    }
    historyGroup->addLayout(history);
    m_colorHistory->hide();
    colorControlsLayout->addWidget(m_colorHistory);

    const auto createSwatches = [&](const QList<QColor> &colors) {
        constexpr int columns = 6;
        auto *group = new QWidget(colorControls);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(6);
        addCaption(groupLayout, group, i18n("Swatches"));
        auto *swatches = new QGridLayout;
        swatches->setSpacing(8);
        for (int index = 0; index < colors.size(); ++index) {
            const QColor color = colors.at(index);
            auto *button = new QPushButton(group);
            button->setMinimumHeight(44);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            const QString hex = color.name().toUpper();
            button->setToolTip(hex);
            button->setAccessibleName(i18n("Set foreground color to %1", hex));
            button->setStyleSheet(colorSwatchStyle(color));
            swatches->addWidget(button, index / columns, index % columns);
            connect(button, &QPushButton::clicked, this, [applyRgb, color] { applyRgb(color); });
        }
        groupLayout->addLayout(swatches);
        colorControlsLayout->addWidget(group);
        return group;
    };
    m_colorSwatches = createSwatches({
        QColor("#15161a"), QColor("#f7f5ee"), QColor("#e95970"), QColor("#e99551"),
        QColor("#e8cc65"), QColor("#5ab49a"), QColor("#58b6c9"), QColor("#748fe3"),
        QColor("#a98adb"), QColor("#d388b3"), QColor("#af7b60"), QColor("#696d79")
    });
    m_graySwatches = createSwatches({
        QColor("#000000"), QColor("#333333"), QColor("#666666"),
        QColor("#999999"), QColor("#cccccc"), QColor("#ffffff")
    });
    m_graySwatches->hide();
    colorControlsLayout->addStretch();

    auto *layerPage = new QWidget(m_pages);
    layerPage->setObjectName("touchPanelPage");
    m_pages->addWidget(layerPage);
    m_layerMenuButton = new QPushButton(QStringLiteral("…"), layerPage);
    m_layerMenuButton->setObjectName("touchLayerMenuButton");
    m_layerMenuButton->setAccessibleName(i18n("Layer actions"));
    m_layerMenuButton->setToolTip(i18n("Layer actions"));
    auto *layerMenu = new QMenu(m_layerMenuButton);
    layerMenu->setStyleSheet(QStringLiteral(R"(
        QMenu { background: #20242c; color: #e5ebf5; border: 1px solid #3a4250; padding: 4px; }
        QMenu::item { padding: 11px 16px; min-height: 20px; border-radius: 5px; }
        QMenu::item:selected { background: #284d7c; color: white; }
        QMenu::item:disabled { color: #8490a3; }
    )"));
    auto *actionManager = viewManager->actionManager();
    for (const QString &name : {QStringLiteral("add_new_group_layer"), QStringLiteral("duplicatelayer"),
                                QStringLiteral("merge_layer"), QStringLiteral("move_layer_up"),
                                QStringLiteral("move_layer_down")}) {
        if (auto *sourceAction = actionManager->actionByName(name)) {
            auto *menuItem = layerMenu->addAction(sourceAction->text());
            connect(menuItem, &QAction::triggered, sourceAction, &KisAction::trigger);
            connect(layerMenu, &QMenu::aboutToShow, menuItem,
                    [this, menuItem, action = QPointer<KisAction>(sourceAction), name] {
                if (!m_viewManager || !action) {
                    menuItem->setEnabled(false);
                    return;
                }
                auto *manager = m_viewManager->nodeManager();
                const KisNodeList selection = manager->selectedNodes();
                const KisNodeSP node = manager->activeNode();
                bool enabled = node && !selection.isEmpty() && action->isEnabled();
                if (name == QLatin1String("add_new_group_layer")) {
                    enabled = action->isEnabled();
                } else if (enabled && (name == QLatin1String("move_layer_up") || name == QLatin1String("move_layer_down"))) {
                    enabled = manager->canMoveLayers(selection, false);
                    if (enabled && selection.size() == 1) {
                        const KisNodeSP selected = selection.front();
                        const bool nested = selected->parent() && selected->parent()->parent();
                        enabled = nested || (name == QLatin1String("move_layer_up")
                            ? bool(selected->nextSibling()) : bool(selected->prevSibling()));
                    }
                }
                menuItem->setText(action->text());
                menuItem->setEnabled(enabled);
            });
        }
    }
    m_layerMenuButton->setMenu(layerMenu);
    m_layerMenuButton->setVisible(!layerMenu->isEmpty());
    auto *layerLayout = pageLayout(layerPage, i18n("Layers"), m_layerMenuButton);
    layerLayout->setSpacing(8);
    m_layerModel = new KisNodeModel(this);
    m_layerView = new QTreeView(layerPage);
    m_layerView->setObjectName(QStringLiteral("touchLayerView"));
    m_layerView->setAccessibleName(i18n("Document layers"));
    m_layerView->setModel(m_layerModel);
    m_layerView->setItemDelegate(new TouchLayerDelegate(m_layerView));
    m_layerView->setHeaderHidden(true);
    m_layerView->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_layerView->setIndentation(20);
    m_layerView->setIconSize(QSize(32, 32));
    m_layerView->setUniformRowHeights(true);
    m_layerView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_layerView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_layerView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(m_layerView, QScroller::TouchGesture);
    layerLayout->addWidget(m_layerView, 1);

    auto *blendLayout = new QHBoxLayout;
    blendLayout->addWidget(new QLabel(i18n("Blend Mode"), layerPage));
    m_blendMode = new KisCompositeOpComboBox(layerPage);
    m_blendMode->setAccessibleName(i18n("Layer blend mode"));
    blendLayout->addWidget(m_blendMode, 1);
    layerLayout->addLayout(blendLayout);
    connect(m_blendMode, qOverload<int>(&QComboBox::activated), this, [this] {
        const KisNodeSP node = m_viewManager->nodeManager()->activeNode();
        if (node && node->compositeOp() && node->isEditable(false)) {
            const auto *op = node->compositeOp()->colorSpace()->compositeOp(m_blendMode->selectedCompositeOp().id());
            if (op) {
                m_viewManager->nodeManager()->setNodeCompositeOp(node, op);
            }
        }
    });

    auto *opacityLayout = new QHBoxLayout;
    auto *opacityLabel = new QLabel(i18n("Opacity"), layerPage);
    m_opacitySlider = new TouchPercentSlider(layerPage);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setMinimumHeight(40);
    m_opacitySlider->setAccessibleName(i18n("Layer opacity"));
    m_opacityValue = new QLabel(layerPage);
    m_opacityValue->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
    m_opacityValue->setMinimumWidth(m_opacityValue->fontMetrics().horizontalAdvance(i18n("%1%", 100)));
    opacityLayout->addWidget(opacityLabel);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);
    layerLayout->addLayout(opacityLayout);
    connect(m_opacitySlider, &QSlider::sliderMoved, this, [this](int opacity) {
        m_opacityValue->setText(i18n("%1%", opacity));
    });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int opacity) {
        const KisNodeSP node = m_opacityNode.toStrongRef();
        if (node && node->isEditable(false) && m_viewManager && m_viewManager->image()
            && node->graphListener() == m_viewManager->image().data()) {
            m_viewManager->nodeManager()->setNodeOpacity(node, qRound(opacity * 255.0 / 100));
        }
        m_opacityValue->setText(i18n("%1%", opacity));
    });

    auto *layerProperties = new QHBoxLayout;
    m_visibilityButton = new QPushButton(layerPage);
    m_lockButton = new QPushButton(layerPage);
    layerProperties->addWidget(m_visibilityButton);
    layerProperties->addWidget(m_lockButton);
    layerLayout->addLayout(layerProperties);

    auto *layerActions = new QHBoxLayout;
    auto *addButton = new QPushButton(i18n("Add Paint Layer"), layerPage);
    addButton->setObjectName("touchAddLayer");
    m_removeButton = new QPushButton(i18n("Delete"), layerPage);
    layerActions->addWidget(addButton, 1);
    layerActions->addWidget(m_removeButton);
    layerLayout->addLayout(layerActions);

    const auto selectLayer = [this](const QModelIndex &index) {
        if (!index.isValid()) {
            return;
        }
        const KisNodeSP node = m_layerModel->nodeFromIndex(index);
        if (node && m_viewManager && m_viewManager->image() && node->graphListener() == m_viewManager->image().data()) {
            auto *manager = m_viewManager->nodeManager();
            manager->slotUiActivatedNode(node);
            if (manager->activeNode() == node) {
                manager->slotSetSelectedNodes(KisNodeList {node});
            }
        }
        syncLayers();
    };
    connect(m_layerView, &QTreeView::clicked, this, selectLayer);
    connect(m_layerView, &QTreeView::activated, this, selectLayer);
    connect(m_layerModel, &QAbstractItemModel::dataChanged, this, &KisTouchStudioPanels::updateLayerButtons);
    connect(m_layerModel, &QAbstractItemModel::modelReset, this, &KisTouchStudioPanels::syncLayers);
    connect(m_layerModel, &QAbstractItemModel::rowsInserted, this, &KisTouchStudioPanels::syncLayers);
    connect(m_layerModel, &QAbstractItemModel::rowsRemoved, this, &KisTouchStudioPanels::syncLayers);

    connect(m_visibilityButton, &QPushButton::clicked, this, [this] {
        toggleLayerProperty(KisLayerPropertiesIcons::visible);
    });
    connect(m_lockButton, &QPushButton::clicked, this, [this] {
        toggleLayerProperty(KisLayerPropertiesIcons::locked);
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        auto *manager = m_viewManager->nodeManager();
        manager->removeSingleNode(manager->activeNode());
    });
    if (auto *action = viewManager->actionManager()->actionByName(QStringLiteral("add_new_paint_layer"))) {
        connect(addButton, &QPushButton::clicked, action, &KisAction::trigger);
        connect(action, &KisAction::changed, addButton, [action, addButton] {
            addButton->setEnabled(action->isEnabled());
        });
        addButton->setEnabled(action->isEnabled());
    } else {
        addButton->setEnabled(false);
    }

    auto *resources = viewManager->canvasResourceProvider();
    auto *paintOpBox = viewManager->paintOpBox();
    connect(m_presets, &KisPresetChooser::resourceSelected, paintOpBox, &KisPaintopBox::resourceSelected);
    connect(m_presets, &KisPresetChooser::resourceClicked, paintOpBox, &KisPaintopBox::resourceSelected);
    const auto syncPreset = [this](const KisPaintOpPresetSP &preset) {
        const QSignalBlocker blocker(m_presets);
        m_presets->setCurrentResource(preset);
    };
    connect(resources, &KisCanvasResourceProvider::sigPaintOpPresetChanged, this, syncPreset);
    syncPreset(resources->currentPreset());

    connect(m_colorSelector, &KisVisualColorSelector::sigNewColor, resources, &KisCanvasResourceProvider::setFGColor);
    connect(resources, &KisCanvasResourceProvider::sigFGColorChanged, this, &KisTouchStudioPanels::syncColor);
    auto *historyModel = resources->colorHistoryModel();
    connect(historyModel, &KisUniqueColorSet::sigReset, this, &KisTouchStudioPanels::updateColorHistory);
    connect(historyModel, &KisUniqueColorSet::sigColorAdded, this, &KisTouchStudioPanels::updateColorHistory);
    connect(historyModel, &KisUniqueColorSet::sigColorMoved, this, &KisTouchStudioPanels::updateColorHistory);
    connect(historyModel, &KisUniqueColorSet::sigColorRemoved, this, &KisTouchStudioPanels::updateColorHistory);
    connect(viewManager->nodeManager(), &KisNodeManager::sigNodeActivated, this, &KisTouchStudioPanels::syncLayers);
    connect(viewManager->nodeManager(), &KisNodeManager::sigUiNeedChangeSelectedNodes, this, &KisTouchStudioPanels::syncLayers);
    connect(viewManager, &KisViewManager::viewChanged, this, &KisTouchStudioPanels::syncDocument);
    syncDocument();
}

KisTouchStudioPanels::~KisTouchStudioPanels()
{
    // Restoring a docker asks QMainWindow to enumerate its child widgets.
    // Return it while the panel and its stacked pages are still intact.
    if (m_dockers) {
        m_dockers->dismiss();
    }
}

QWidget *KisTouchStudioPanels::createBrushSettings(QWidget *parent)
{
    auto *page = new QScrollArea(parent);
    page->setWidgetResizable(true);
    page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(page->viewport(), QScroller::TouchGesture);
    auto *contents = new QWidget(page);
    contents->setObjectName(QStringLiteral("touchBrushSettings"));
    page->setWidget(contents);
    auto *layout = new QVBoxLayout(contents);
    layout->setContentsMargins(10, 12, 10, 12);
    layout->setSpacing(12);

    auto *resources = m_viewManager->canvasResourceProvider();
    auto *presetName = new QLabel(contents);
    presetName->setTextFormat(Qt::PlainText);
    presetName->setWordWrap(true);
    QFont presetFont = presetName->font();
    presetFont.setWeight(QFont::DemiBold);
    presetName->setFont(presetFont);
    layout->addWidget(presetName);

    auto *blend = new KisCompositeOpComboBox(contents);
    blend->setAccessibleName(i18n("Brush blend mode"));
    auto *blendLabel = new QLabel(i18n("Blend Mode"), contents);
    blendLabel->setBuddy(blend);
    layout->addWidget(blendLabel);
    layout->addWidget(blend);
    connect(blend, qOverload<int>(&QComboBox::activated), blend, [resources, blend] {
        const QString id = blend->selectedCompositeOp().id();
        if (!id.isEmpty()) {
            resources->setCurrentCompositeOp(id);
        }
    });
    const auto syncBlend = [this, resources, blend] {
        const QSignalBlocker blocker(blend);
        const KisPaintDeviceSP device = m_viewManager->activeDevice();
        const auto *colorSpace = device ? device->compositionSourceColorSpace() : nullptr;
        QString mode = resources->currentCompositeOp();
        if (colorSpace) {
            blend->validate(colorSpace);
            if (!colorSpace->hasCompositeOp(mode)) {
                mode.clear();
            }
        }
        const KoID op = mode.isEmpty() ? KoCompositeOpRegistry::instance().getDefaultCompositeOp()
                                       : KoCompositeOpRegistry::instance().getKoID(mode);
        blend->selectCompositeOp(op);
        blend->setEnabled(bool(resources->currentPreset()));
    };
    connect(resources, &KisCanvasResourceProvider::sigNodeChanged, blend, syncBlend);
    connect(m_viewManager, &KisViewManager::viewChanged, blend, syncBlend);

    auto *flowLayout = new QHBoxLayout;
    auto *flowLabel = new QLabel(i18n("Flow"), contents);
    auto *flow = new TouchPercentSlider(contents);
    flow->setRange(0, 100);
    flow->setMinimumHeight(40);
    flow->setAccessibleName(i18n("Brush flow"));
    flowLabel->setBuddy(flow);
    auto *flowValue = new QLabel(contents);
    flowValue->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
    flowValue->setMinimumWidth(flowValue->fontMetrics().horizontalAdvance(i18n("%1%", 100)));
    flowLayout->addWidget(flowLabel);
    flowLayout->addWidget(flow, 1);
    flowLayout->addWidget(flowValue);
    layout->addLayout(flowLayout);
    connect(flow, &QSlider::sliderMoved, flowValue, [flowValue](int value) {
        flowValue->setText(i18n("%1%", value));
    });
    connect(flow, &QSlider::valueChanged, flow, [resources, flowValue](int value) {
        if (resources->currentPreset()) {
            resources->setFlow(value / 100.0);
        }
        flowValue->setText(i18n("%1%", value));
    });
    const auto syncFlow = [resources, flow, flowValue] {
        flow->setEnabled(bool(resources->currentPreset()));
        if (!flow->isSliderDown()) {
            const QSignalBlocker blocker(flow);
            const int value = qRound(resources->flow() * 100);
            flow->setValue(value);
            flowValue->setText(i18n("%1%", value));
        }
    };
    connect(resources->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
            page, [syncBlend, syncFlow](int key) {
        if (key == KoCanvasResource::Flow) {
            syncFlow();
        } else if (key == KoCanvasResource::CurrentCompositeOp) {
            syncBlend();
        }
    });
    const auto syncPreset = [presetName, flow, syncFlow, syncBlend](const KisPaintOpPresetSP &preset) {
        const QSignalBlocker blocker(flow);
        flow->setSliderDown(false);
        presetName->setText(preset ? preset->name() : QString());
        presetName->setVisible(bool(preset));
        syncFlow();
        syncBlend();
    };
    connect(resources, &KisCanvasResourceProvider::sigPaintOpPresetChanged, page, syncPreset);
    syncPreset(resources->currentPreset());

    auto *smoothingLabel = new QLabel(i18n("Brush Smoothing"), contents);
    m_brushSmoothing = new QComboBox(contents);
    smoothingLabel->setBuddy(m_brushSmoothing);
    m_brushSmoothing->setAccessibleName(i18n("Brush smoothing"));
    m_brushSmoothing->setToolTip(i18n("Available for the Freehand Brush tool"));
    const QString smoothingLabels[] = {i18n("Disabled"), i18n("Basic"), i18n("Weighted"),
                                       i18n("Stabilizer"), i18n("Pixel Perfect")};
    const int smoothingTypes[] = {KisSmoothingOptions::NO_SMOOTHING, KisSmoothingOptions::SIMPLE_SMOOTHING,
                                  KisSmoothingOptions::WEIGHTED_SMOOTHING, KisSmoothingOptions::STABILIZER,
                                  KisSmoothingOptions::PIXEL_PERFECT};
    for (size_t index = 0; index < std::size(smoothingActions); ++index) {
        m_brushSmoothing->addItem(smoothingLabels[index], smoothingTypes[index]);
    }
    layout->addWidget(smoothingLabel);
    layout->addWidget(m_brushSmoothing);
    connect(m_brushSmoothing, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (m_brushSmoothingTool && index >= 0 && size_t(index) < std::size(smoothingActions)) {
            if (auto *action = m_brushSmoothingTool->action(QLatin1String(smoothingActions[index]))) {
                action->trigger();
            }
        }
        syncBrushSmoothing();
    });
    connect(m_viewManager->toolManager(), &KoToolManager::changedTool, this, &KisTouchStudioPanels::updateBrushSmoothingTool);
    connect(m_viewManager, &KisViewManager::viewChanged, this, &KisTouchStudioPanels::updateBrushSmoothingTool);
    updateBrushSmoothingTool();

    const auto makeActionButton = [this, contents, layout](const QString &text, const QString &id) {
        auto *action = m_viewManager->actionCollection()->action(id);
        if (action) {
            auto *button = new QPushButton(text, contents);
            button->setEnabled(action->isEnabled());
            connect(button, &QPushButton::clicked, action, &QAction::trigger);
            connect(action, &QAction::changed, button, [action, button] {
                button->setEnabled(action->isEnabled());
            });
            layout->addWidget(button);
        }
    };
    makeActionButton(i18n("Reload Original Preset"), QStringLiteral("reload_preset_action"));
    makeActionButton(i18n("Open Brush Editor"), QStringLiteral("show_brush_editor"));
    layout->addStretch(1);
    return page;
}

void KisTouchStudioPanels::updateBrushSmoothingTool()
{
    disconnect(m_brushSmoothingChanged);
    auto *toolManager = m_viewManager->toolManager();
    const QString activeTool = toolManager->activeToolId();
    auto *canvas = m_viewManager->canvasBase();
    auto *tool = canvas && !activeTool.isEmpty() ? toolManager->toolById(canvas, activeTool) : nullptr;
    m_brushSmoothingTool = tool && tool->metaObject()->indexOfProperty("smoothingType") >= 0 ? tool : nullptr;
    if (m_brushSmoothingTool) {
        m_brushSmoothingChanged = connect(m_brushSmoothingTool, SIGNAL(smoothingTypeChanged()), this, SLOT(syncBrushSmoothing()));
    }
    syncBrushSmoothing();
}

void KisTouchStudioPanels::syncBrushSmoothing()
{
    const QSignalBlocker blocker(m_brushSmoothing);
    m_brushSmoothing->setEnabled(bool(m_brushSmoothingTool));
    const int index = m_brushSmoothingTool
        ? m_brushSmoothing->findData(m_brushSmoothingTool->property("smoothingType")) : -1;
    m_brushSmoothing->setCurrentIndex(index);
}

void KisTouchStudioPanels::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const bool wideLibrary = event->size().width() >= 380;
    m_brushTags->setVisible(wideLibrary);
    m_compactTags->setVisible(!wideLibrary);
}

QSize KisTouchStudioPanels::sizeHint() const
{
    if (m_pages && m_pages->currentIndex() == 1 && m_colorIsGray) {
        return QSize(396, m_colorHistory->isHidden() ? 330 : 410);
    }
    return QSize(396, 560);
}

QSize KisTouchStudioPanels::minimumSizeHint() const
{
    return QSize(284, m_pages && m_pages->currentIndex() == 2 ? 310 : 240);
}

void KisTouchStudioPanels::showPage(const QString &id)
{
    if (m_dockers && m_pages->currentWidget() == m_dockers) {
        m_dockers->dismiss();
    }

    if (id == QLatin1String("brushes")) {
        m_pages->setCurrentIndex(0);
    } else if (id == QLatin1String("color")) {
        m_pages->setCurrentIndex(1);
    } else if (id == QLatin1String("layers")) {
        m_pages->setCurrentIndex(2);
    } else if (id == QLatin1String("studio") || id == QLatin1String("options")) {
        if (!m_dockers) {
            m_dockers = new KisTouchStudioDockers(m_viewManager, m_pages);
            m_dockers->setPalette(QApplication::palette());
            m_pages->addWidget(m_dockers);
        }
        m_showToolOptions = id == QLatin1String("options");
        m_pages->setCurrentWidget(m_dockers);
        if (isVisible()) {
            m_dockers->showToolOptions(m_showToolOptions);
        }
    }
}

void KisTouchStudioPanels::hideEvent(QHideEvent *event)
{
    if (m_dockers) {
        m_dockers->dismiss();
    }
    QWidget::hideEvent(event);
}

void KisTouchStudioPanels::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_dockers && m_pages->currentWidget() == m_dockers) {
        m_dockers->showToolOptions(m_showToolOptions);
    }
}

void KisTouchStudioPanels::syncDocument()
{
    disconnect(m_imageClosing);
    disconnect(m_colorSpaceChanged);
    disconnect(m_displayChanged);
    m_layerModel->setIdleTaskManager(nullptr);
    m_layerModel->setDummiesFacade(nullptr, nullptr, nullptr, nullptr, nullptr);

    KisImageWSP image = m_viewManager->image();
    setEnabled(bool(image));
    auto *canvas = m_viewManager->canvasBase();
    m_colorSelector->setDisplayRenderer(canvas ? canvas->displayRendererInterface() : nullptr);
    auto *document = m_viewManager->document();
    if (!image || !document) {
        syncColor(m_viewManager->canvasResourceProvider()->fgColor());
        return;
    }

    auto *shapeController = dynamic_cast<KisShapeController *>(document->shapeController());
    m_layerModel->setDummiesFacade(shapeController, image, shapeController, nullptr, m_viewManager->nodeManager());
    m_layerModel->setPreferredThumnalSize(qRound(44 * m_layerView->devicePixelRatioF()));
    m_layerModel->setIdleTaskManager(m_viewManager->idleTasksManager());
    m_imageClosing = connect(image.data(), &KisImage::sigAboutToBeDeleted, this, [this] {
        disconnect(m_colorSpaceChanged);
        disconnect(m_displayChanged);
        m_layerModel->setIdleTaskManager(nullptr);
        m_layerModel->setDummiesFacade(nullptr, nullptr, nullptr, nullptr, nullptr);
        m_colorSelector->setDisplayRenderer(nullptr);
        setEnabled(false);
    });

    setColorSpace(image->colorSpace());
    syncColor(m_viewManager->canvasResourceProvider()->fgColor());
    m_colorSpaceChanged = connect(image.data(), &KisImage::sigColorSpaceChanged, this, [this](const KoColorSpace *space) {
        setColorSpace(space);
        syncColor(m_viewManager->canvasResourceProvider()->fgColor());
        updateColorHistory();
    });
    m_displayChanged = connect(m_colorSelector->displayRenderer(), &KoColorDisplayRendererInterface::displayConfigurationChanged,
                               this, [this] {
        syncColor(m_viewManager->canvasResourceProvider()->fgColor());
        updateColorHistory();
    });
    updateColorHistory();
    syncLayers();
}

void KisTouchStudioPanels::setColorSpace(const KoColorSpace *space)
{
    m_colorSelector->slotSetColorSpace(space);
    m_colorIsGray = space->colorChannelCount() == 1;
    m_colorSelector->setMinimumHeight(m_colorIsGray ? 52 : 226);
    m_colorSelector->setMaximumHeight(m_colorIsGray ? 52 : 300);
    m_colorRgbFields->setVisible(!m_colorIsGray);
    m_colorSwatches->setVisible(!m_colorIsGray);
    m_graySwatches->setVisible(m_colorIsGray);
}

void KisTouchStudioPanels::syncColor(const KoColor &color)
{
    const QSignalBlocker blocker(m_colorSelector);
    m_colorSelector->slotSetColor(color);
    KoColor paintingColor = color;
    if (m_colorIsGray && m_viewManager->image()) {
        paintingColor.convertTo(m_viewManager->image()->colorSpace());
    }
    const QString displayHex = m_colorSelector->displayRenderer()->toQColor(paintingColor).name();
    m_colorSample->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #565960; border-radius: 7px;").arg(displayHex));
    const QColor rgb = paintingColor.toQColor();
    m_colorValue->setText(rgb.name().toUpper());
    const int channels[] = {rgb.red(), rgb.green(), rgb.blue()};
    for (int channel = 0; channel < 3; ++channel) {
        m_colorChannels[channel]->setText(QString::number(channels[channel]));
    }
}

void KisTouchStudioPanels::updateColorHistory()
{
    auto *history = m_viewManager->canvasResourceProvider()->colorHistoryModel();
    const auto *renderer = m_colorSelector->displayRenderer();
    m_colorHistory->setVisible(history->size() > 0);
    for (int index = 0; index < 6; ++index) {
        auto *button = m_historyButtons[index];
        const bool available = index < history->size();
        button->setVisible(available);
        if (available) {
            KoColor color = history->color(index);
            if (m_colorIsGray && m_viewManager->image()) {
                color.convertTo(m_viewManager->image()->colorSpace());
            }
            const QColor swatch = renderer->toQColor(color);
            const QString hex = swatch.name().toUpper();
            button->setStyleSheet(colorSwatchStyle(swatch));
            button->setToolTip(hex);
            button->setAccessibleName(i18n("Use recent color %1", hex));
        }
    }
}

void KisTouchStudioPanels::syncLayers()
{
    if (!m_viewManager || !m_layerModel->hasDummiesFacade()) {
        updateLayerButtons();
        return;
    }

    auto *manager = m_viewManager->nodeManager();
    const QModelIndex active = m_layerModel->indexFromNode(manager->activeNode());
    auto *selectionModel = m_layerView->selectionModel();
    QItemSelection selection;
    const KisNodeList selected = manager->selectedNodes();
    for (const KisNodeSP &node : selected) {
        const QModelIndex index = m_layerModel->indexFromNode(node);
        if (index.isValid()) {
            selection.select(index, index);
        }
    }
    if (selection.isEmpty() && active.isValid()) {
        selection.select(active, active);
    }

    selectionModel->setCurrentIndex(active, QItemSelectionModel::NoUpdate);
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    for (QModelIndex ancestor = active.parent(); ancestor.isValid(); ancestor = ancestor.parent()) {
        m_layerView->expand(ancestor);
    }
    if (active.isValid() && m_layerView->isVisible()) {
        m_layerView->scrollTo(active, QAbstractItemView::EnsureVisible);
    }
    updateLayerButtons();
}

void KisTouchStudioPanels::updateLayerButtons()
{
    const QModelIndex index = m_layerView->currentIndex();
    const KisNodeSP node = index.isValid() && m_layerModel->hasDummiesFacade()
        ? m_layerModel->nodeFromIndex(index) : KisNodeSP();
    const QVariant visible = node
        ? KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::visible, QVariant()) : QVariant();
    const QVariant locked = node
        ? KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::locked, QVariant()) : QVariant();
    m_visibilityButton->setText(visible.toBool() ? i18n("Hide Layer") : i18n("Show Layer"));
    m_lockButton->setText(locked.toBool() ? i18n("Unlock Layer") : i18n("Lock Layer"));
    m_visibilityButton->setEnabled(visible.isValid());
    m_lockButton->setEnabled(locked.isValid());
    m_removeButton->setEnabled(node && node->parent() && m_viewManager->nodeManager()->canModifyLayer(node, false));
    m_layerMenuButton->setEnabled(bool(node));
    const auto *group = node ? qobject_cast<const KisGroupLayer *>(node.data()) : nullptr;
    const bool supportsBlend = node && node->compositeOp() && !(group && group->passThroughMode());
    m_blendMode->setEnabled(supportsBlend && node->isEditable(false));
    if (supportsBlend) {
        const QSignalBlocker blendBlocker(m_blendMode);
        m_blendMode->validate(node->compositeOp()->colorSpace());
        m_blendMode->selectCompositeOp(KoCompositeOpRegistry::instance().getKoID(node->compositeOpId()));
    }

    if (node != m_opacityNode.toStrongRef()) {
        const QSignalBlocker blocker(m_opacitySlider);
        m_opacitySlider->setSliderDown(false);
        disconnect(m_layerOpacityChanged);
        m_opacityNode = node;
        if (node) {
            m_layerOpacityChanged = connect(node.data(), &KisNode::opacityChanged,
                                            this, &KisTouchStudioPanels::updateLayerOpacity);
        }
    }
    updateLayerOpacity();
}

void KisTouchStudioPanels::updateLayerOpacity()
{
    const KisNodeSP node = m_opacityNode.toStrongRef();
    const bool supportsOpacity = node && (node->inherits("KisLayer") || node->inherits("KisColorizeMask"));
    m_opacitySlider->setEnabled(supportsOpacity && node->isEditable(false));
    if (m_opacitySlider->isSliderDown()) {
        return;
    }

    const QSignalBlocker blocker(m_opacitySlider);
    const int opacity = supportsOpacity ? qRound(node->opacity() * 100.0 / 255) : 0;
    m_opacitySlider->setValue(opacity);
    m_opacityValue->setText(supportsOpacity ? i18n("%1%", opacity) : QString());
}

void KisTouchStudioPanels::toggleLayerProperty(const KoID &property)
{
    const QModelIndex index = m_layerView->currentIndex();
    auto properties = index.data(KisNodeModel::PropertiesRole).value<KisBaseNode::PropertyList>();
    for (const KisBaseNode::Property &item : properties) {
        if (item.id == property.id()) {
            KisLayerPropertiesIcons::setNodeProperty(&properties, property, !item.state.toBool());
            m_layerModel->setData(index, QVariant::fromValue(properties), KisNodeModel::PropertiesRole);
            return;
        }
    }
}
