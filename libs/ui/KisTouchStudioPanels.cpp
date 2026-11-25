/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioPanels.h"

#include <QGridLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KisColorSelectorConfiguration.h>
#include <KisResourceItemChooser.h>
#include <KisResourceItemListView.h>
#include <KisVisualColorSelector.h>
#include <KoColorDisplayRendererInterface.h>
#include <KoColorSpaceRegistry.h>
#include <kis_image.h>
#include <kis_layer_properties_icons.h>
#include <kis_node.h>
#include <kis_paintop_preset.h>

#include "KisDocument.h"
#include "KisViewManager.h"
#include "kis_action.h"
#include "kis_action_manager.h"
#include "kis_canvas2.h"
#include "kis_canvas_resource_provider.h"
#include "kis_node_manager.h"
#include "kis_node_model.h"
#include "kis_paintop_box.h"
#include "kis_preset_chooser.h"
#include "kis_shape_controller.h"

namespace
{
class OpacitySlider final : public QSlider
{
public:
    explicit OpacitySlider(QWidget *parent)
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

class TouchLayerDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(0, 48);
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const int pixelSize = qRound(32 * (option->widget ? option->widget->devicePixelRatioF() : 1.0));
        const QImage thumbnail = index.data(KisNodeModel::BeginThumbnailRole + pixelSize).value<QImage>();
        if (!thumbnail.isNull()) {
            option->icon = QIcon(QPixmap::fromImage(thumbnail));
            option->decorationSize = QSize(32, 32);
            option->features |= QStyleOptionViewItem::HasDecoration;
        }
        if (index.data(KisNodeModel::ShouldGrayOutRole).toBool()) {
            option->palette.setColor(QPalette::Text, QColor("#a9b5c8"));
        }
    }
};

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
    panelPalette.setColor(QPalette::Window, QColor("#20242c"));
    panelPalette.setColor(QPalette::WindowText, QColor("#e5ebf5"));
    panelPalette.setColor(QPalette::Base, QColor("#171b22"));
    panelPalette.setColor(QPalette::Text, QColor("#e5ebf5"));
    panelPalette.setColor(QPalette::Button, QColor("#343c4a"));
    panelPalette.setColor(QPalette::ButtonText, QColor("#e5ebf5"));
    panelPalette.setColor(QPalette::Mid, QColor("#3a4250"));
    panelPalette.setColor(QPalette::Highlight, QColor("#83baff"));
    panelPalette.setColor(QPalette::HighlightedText, QColor("#111d2e"));
    setPalette(panelPalette);
    setStyleSheet(QStringLiteral(R"(
        #touchStudioPanels {
            background: #20242c;
            border: 1px solid #3a4250;
            border-radius: 14px;
        }
        #touchStudioPanels QLabel, #touchStudioPanels QAbstractItemView {
            color: #e5ebf5;
        }
        #touchStudioPanels #touchPanelHeading {
            font-size: 20px;
            font-weight: bold;
        }
        #touchStudioPanels #touchColorValue, #touchStudioPanels #touchPresetName {
            color: #b8c5d8;
        }
        #touchStudioPanels QAbstractScrollArea {
            background: #171b22;
            border: 0;
            border-radius: 8px;
        }
        #touchStudioPanels QAbstractItemView {
            selection-background-color: #284d7c;
            selection-color: #ffffff;
        }
        #touchStudioPanels #touchPresetView {
            selection-background-color: #83baff;
            selection-color: #111d2e;
        }
        #touchStudioPanels QPushButton {
            background: #343c4a;
            border: 1px solid #424c5b;
            border-radius: 8px;
            color: #e5ebf5;
            min-height: 40px;
            padding: 0 8px;
        }
        #touchStudioPanels QPushButton:hover, #touchStudioPanels QPushButton:focus {
            border-color: #83baff;
        }
        #touchStudioPanels QPushButton:pressed {
            background: #3b6190;
        }
        #touchStudioPanels QPushButton:disabled {
            color: #8490a3;
        }
        #touchStudioPanels #touchAddLayer {
            background: #83baff;
            color: #111d2e;
            font-weight: bold;
        }
        #touchStudioPanels #touchAddLayer:disabled {
            background: #343c4a;
            color: #8490a3;
        }
        #touchStudioPanels #touchLayerMenuButton {
            min-width: 42px;
            padding: 0;
        }
        #touchStudioPanels #touchLayerMenuButton::menu-indicator {
            image: none;
        }
        #touchStudioPanels QSlider::groove:horizontal {
            background: #14171d;
            border: 1px solid #424c5b;
            height: 8px;
            border-radius: 5px;
        }
        #touchStudioPanels QSlider::handle:horizontal {
            background: #83baff;
            border: 1px solid #c6e0ff;
            border-radius: 11px;
            width: 22px;
            margin: -7px 0;
        }
        #touchStudioPanels QSlider::handle:horizontal:focus {
            background: #b9d9ff;
        }
        #touchStudioPanels QSlider::handle:horizontal:disabled {
            background: #5b6980;
            border-color: #6e7a8d;
        }
        #touchStudioPanels QScrollBar:vertical {
            background: transparent;
            width: 12px;
        }
        #touchStudioPanels QScrollBar::handle:vertical {
            background: #5b6980;
            border-radius: 4px;
            min-height: 32px;
            margin: 0 2px;
        }
        #touchStudioPanels QScrollBar::add-line:vertical,
        #touchStudioPanels QScrollBar::sub-line:vertical {
            height: 0;
        }
        #touchStudioPanels QScrollBar::add-page:vertical,
        #touchStudioPanels QScrollBar::sub-page:vertical {
            background: transparent;
        }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    auto *brushPage = new QWidget(m_pages);
    m_pages->addWidget(brushPage);
    auto *brushLayout = pageLayout(brushPage, i18n("Brush Presets"));
    m_presets = new KisPresetChooser(brushPage);
    m_presets->setViewModeToThumbnail();
    m_presets->itemChooser()->setSynced(false);
    m_presets->itemChooser()->showStorageBtn(false);
    auto *presetView = m_presets->itemChooser()->itemView();
    presetView->setObjectName("touchPresetView");
    presetView->setAccessibleName(i18n("Brush presets"));
    presetView->setGridSize(QSize(62, 62));
    presetView->setIconSize(QSize(62, 62));
    presetView->setMinimumSize(0, 0);
    brushLayout->addWidget(m_presets, 1);
    auto *presetName = new QLabel(brushPage);
    presetName->setObjectName("touchPresetName");
    presetName->setTextFormat(Qt::PlainText);
    presetName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    brushLayout->addWidget(presetName);

    auto *colorPage = new QWidget(m_pages);
    m_pages->addWidget(colorPage);
    auto *colorLayout = pageLayout(colorPage, i18n("Foreground Color"));
    auto *colorScroll = new QScrollArea(colorPage);
    colorScroll->setWidgetResizable(true);
    colorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    colorLayout->addWidget(colorScroll, 1);

    auto *colorControls = new QWidget(colorScroll);
    colorScroll->setWidget(colorControls);
    auto *colorControlsLayout = new QVBoxLayout(colorControls);
    colorControlsLayout->setContentsMargins(8, 8, 8, 8);
    colorControlsLayout->setSpacing(12);
    m_colorSelector = new KisVisualColorSelector(colorControls);
    const KisColorSelectorConfiguration colorConfiguration(
        KisColorSelectorConfiguration::Square, KisColorSelectorConfiguration::Slider,
        KisColorSelectorConfiguration::SV, KisColorSelectorConfiguration::H);
    m_colorSelector->setConfiguration(&colorConfiguration);
    m_colorSelector->setSliderPosition(Qt::TopEdge);
    m_colorSelector->setMinimumSliderWidth(32);
    m_colorSelector->setMinimumHeight(144);
    m_colorSelector->setAcceptTabletEvents(true);
    m_colorSelector->setAccessibleName(i18n("Foreground color selector"));
    colorControlsLayout->addWidget(m_colorSelector, 1);

    auto *colorReadout = new QHBoxLayout;
    m_colorSample = new QLabel(colorControls);
    m_colorSample->setFixedSize(40, 32);
    m_colorValue = new QLabel(colorControls);
    m_colorValue->setObjectName("touchColorValue");
    colorReadout->addWidget(m_colorSample);
    colorReadout->addWidget(m_colorValue);
    colorReadout->addStretch();
    colorControlsLayout->addLayout(colorReadout);

    auto *swatches = new QGridLayout;
    const QColor swatchColors[] = {
        QColor("#16151b"), QColor("#ffffff"), QColor("#eb4b69"),
        QColor("#f2a24b"), QColor("#eed462"), QColor("#46bc91"),
        QColor("#46bddb"), QColor("#6889ef"), QColor("#a886ed"),
        QColor("#e18ac7"), QColor("#b37d67"), QColor("#6e7184")
    };
    constexpr int swatchColumns = 6;
    int index = 0;
    for (const QColor &color : swatchColors) {
        auto *button = new QPushButton(colorControls);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setToolTip(color.name(QColor::HexRgb));
        button->setAccessibleName(i18n("Set foreground color to %1", color.name(QColor::HexRgb)));
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; padding: 0; }")
                                  .arg(color.name(QColor::HexRgb)));
        swatches->addWidget(button, index / swatchColumns, index % swatchColumns);
        ++index;
        connect(button, &QPushButton::clicked, this, [this, color] {
            m_viewManager->canvasResourceProvider()->setFGColor(
                KoColor(color, KoColorSpaceRegistry::instance()->rgb8()));
        });
    }
    colorControlsLayout->addLayout(swatches);

    auto *layerPage = new QWidget(m_pages);
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
    for (const QString &name : {QStringLiteral("duplicatelayer"),
                                QStringLiteral("move_layer_up"), QStringLiteral("move_layer_down")}) {
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
                if (enabled && name != QLatin1String("duplicatelayer")) {
                    enabled = manager->canMoveLayers(selection, false);
                    if (enabled && selection.size() == 1) {
                        const KisNodeSP selected = selection.front();
                        const bool nested = selected->parent() && selected->parent()->parent();
                        enabled = nested || (name == QLatin1String("move_layer_up")
                            ? bool(selected->nextSibling()) : bool(selected->prevSibling()));
                    }
                }
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

    auto *opacityLayout = new QHBoxLayout;
    auto *opacityLabel = new QLabel(i18n("Opacity"), layerPage);
    m_opacitySlider = new OpacitySlider(layerPage);
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
    const auto syncPreset = [this, presetName](const KisPaintOpPresetSP &preset) {
        const QSignalBlocker blocker(m_presets);
        m_presets->setCurrentResource(preset);
        const QString name = preset ? preset->name() : QString();
        presetName->setText(name);
        presetName->setToolTip(name.toHtmlEscaped());
    };
    connect(resources, &KisCanvasResourceProvider::sigPaintOpPresetChanged, this, syncPreset);
    syncPreset(resources->currentPreset());

    connect(m_colorSelector, &KisVisualColorSelector::sigNewColor, resources, &KisCanvasResourceProvider::setFGColor);
    connect(resources, &KisCanvasResourceProvider::sigFGColorChanged, this, &KisTouchStudioPanels::syncColor);
    connect(viewManager->nodeManager(), &KisNodeManager::sigNodeActivated, this, &KisTouchStudioPanels::syncLayers);
    connect(viewManager->nodeManager(), &KisNodeManager::sigUiNeedChangeSelectedNodes, this, &KisTouchStudioPanels::syncLayers);
    connect(viewManager, &KisViewManager::viewChanged, this, &KisTouchStudioPanels::syncDocument);
    syncDocument();
}

QSize KisTouchStudioPanels::sizeHint() const
{
    return QSize(328, 416);
}

QSize KisTouchStudioPanels::minimumSizeHint() const
{
    return QSize(240, m_pages->currentIndex() == 2 ? 260 : 180);
}

void KisTouchStudioPanels::showPage(const QString &id)
{
    if (id == QLatin1String("brushes")) {
        m_pages->setCurrentIndex(0);
    } else if (id == QLatin1String("color")) {
        m_pages->setCurrentIndex(1);
    } else if (id == QLatin1String("layers")) {
        m_pages->setCurrentIndex(2);
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
    m_layerModel->setPreferredThumnalSize(qRound(32 * m_layerView->devicePixelRatioF()));
    m_layerModel->setIdleTaskManager(m_viewManager->idleTasksManager());
    m_imageClosing = connect(image.data(), &KisImage::sigAboutToBeDeleted, this, [this] {
        disconnect(m_colorSpaceChanged);
        disconnect(m_displayChanged);
        m_layerModel->setIdleTaskManager(nullptr);
        m_layerModel->setDummiesFacade(nullptr, nullptr, nullptr, nullptr, nullptr);
        m_colorSelector->setDisplayRenderer(nullptr);
        setEnabled(false);
    });

    m_colorSelector->slotSetColorSpace(image->colorSpace());
    syncColor(m_viewManager->canvasResourceProvider()->fgColor());
    m_colorSpaceChanged = connect(image.data(), &KisImage::sigColorSpaceChanged, this, [this](const KoColorSpace *space) {
        m_colorSelector->slotSetColorSpace(space);
        syncColor(m_viewManager->canvasResourceProvider()->fgColor());
    });
    m_displayChanged = connect(m_colorSelector->displayRenderer(), &KoColorDisplayRendererInterface::displayConfigurationChanged,
                               this, [this] { syncColor(m_viewManager->canvasResourceProvider()->fgColor()); });
    syncLayers();
}

void KisTouchStudioPanels::syncColor(const KoColor &color)
{
    const QSignalBlocker blocker(m_colorSelector);
    m_colorSelector->slotSetColor(color);
    const QString hex = m_colorSelector->displayRenderer()->toQColor(color).name(QColor::HexRgb).toUpper();
    m_colorSample->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #5b6980; border-radius: 6px;").arg(hex));
    m_colorValue->setText(hex);
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
