/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudio.h"

#include <QAction>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <kactioncollection.h>
#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>
#include <KoColorDisplayRendererInterface.h>
#include <KoToolManager.h>

#include "KisTouchStudioAnimation.h"
#include "KisTouchStudioPanels.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "kis_canvas_resource_provider.h"
#include "kis_icon_utils.h"
#include "kis_image_config.h"

namespace
{
constexpr int edge = 12;
constexpr int gap = 8;
constexpr int buttonSize = 44;
constexpr int padding = 6;
constexpr int spacing = 2;

class TouchSlider : public QSlider
{
public:
    explicit TouchSlider(QWidget *parent)
        : QSlider(Qt::Vertical, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setSliderDown(true);
        moveTo(event->pos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown()) {
            moveTo(event->pos());
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->pos());
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void moveTo(const QPoint &point)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const bool horizontal = orientation() == Qt::Horizontal;
        const int handleLength = horizontal ? handle.width() : handle.height();
        const int start = horizontal ? groove.x() : groove.y();
        const int span = (horizontal ? groove.width() : groove.height()) - handleLength;
        const int position = (horizontal ? point.x() : point.y()) - start - handleLength / 2;
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(), maximum(), position, span, option.upsideDown));
    }
};

void matchCanvasSurface(QWidget *overlay, KisViewManager *manager)
{
    // A native color-managed canvas stacks above alien siblings. Give the
    // controls their own surfaces in that configuration, without making the
    // rest of the view native or routing touches through the drawing canvas.
    if (manager->canvasBase()->canvasWidget()->testAttribute(Qt::WA_NativeWindow)) {
        overlay->setAttribute(Qt::WA_DontCreateNativeAncestors);
        overlay->setAttribute(Qt::WA_NativeWindow);
    }
}

QWidget *makeBar(QWidget *parent, KisViewManager *manager)
{
    auto *bar = new QWidget(parent);
    matchCanvasSurface(bar, manager);
    bar->setObjectName(QStringLiteral("touchStudioBar"));
    bar->setAttribute(Qt::WA_NoMousePropagation);
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setStyleSheet(QStringLiteral(R"(
        QWidget#touchStudioBar {
            background-color: #20242c;
            border: 1px solid #3a4250;
            border-radius: 16px;
        }
        QToolButton {
            background: transparent;
            color: #e5ebf5;
            border: 1px solid transparent;
            border-radius: 10px;
        }
        QToolButton:hover { background: #343c4a; }
        QToolButton:checked { background: #284d7c; border-color: #579ef4; }
        QToolButton:pressed { background: #3b6190; }
        QToolButton:focus { border-color: #84bafd; }
        QToolButton:disabled { color: #768093; }
        QLabel { color: #d5dce8; }
        QSlider::groove:vertical {
            background: #14171d; border: 1px solid #424c5b;
            width: 8px; border-radius: 5px;
        }
        QSlider::groove:horizontal {
            background: #14171d; border: 1px solid #424c5b;
            height: 8px; border-radius: 5px;
        }
        QSlider::handle {
            background: #83baff; border: 1px solid #c6e0ff; border-radius: 9px;
        }
        QSlider::handle:vertical { height: 18px; margin: 0 -5px; }
        QSlider::handle:horizontal { width: 18px; margin: -5px 0; }
        QSlider::handle:pressed, QSlider::handle:focus { background: #b9d9ff; }
    )"));
    return bar;
}

QIcon barIcon(const QString &name)
{
    const QString lightPath = QStringLiteral(":/light_%1.svg").arg(name);
    if (QFile::exists(lightPath)) {
        return QIcon(lightPath);
    }
    return KisIconUtils::loadIcon(name);
}

QToolButton *makeButton(QWidget *parent, const QString &text, const QString &iconName)
{
    auto *button = new QToolButton(parent);
    button->setFixedSize(buttonSize, buttonSize);
    button->setIconSize(QSize(22, 22));
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setFocusPolicy(Qt::TabFocus);
    const QIcon icon = barIcon(iconName);
    if (icon.isNull()) {
        button->setText(text.left(3));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    } else {
        button->setIcon(icon);
    }
    return button;
}

void bindAction(QToolButton *button, QAction *action)
{
    if (!action) {
        button->setEnabled(false);
        return;
    }

    // Mirror the real action without replacing the toolbar's themed icon or label.
    const auto sync = [button, action] {
        button->setEnabled(action->isEnabled());
        button->setCheckable(action->isCheckable());
        button->setChecked(action->isChecked());
    };
    QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
    QObject::connect(action, &QAction::changed, button, sync);
    QObject::connect(action, &QObject::destroyed, button, [button] {
        button->setEnabled(false);
    });
    sync();
}

QGridLayout *makeButtonGrid(QWidget *bar)
{
    auto *layout = new QGridLayout(bar);
    layout->setContentsMargins(padding, padding, padding, padding);
    layout->setSpacing(spacing);
    return layout;
}

void arrangeButtons(QGridLayout *layout, const QList<QToolButton *> &buttons, int columns)
{
    for (QToolButton *button : buttons) {
        layout->removeWidget(button);
    }
    for (int i = 0; i < buttons.size(); ++i) {
        layout->addWidget(buttons.at(i), i / columns, i % columns);
    }
}

int buttonsPerRow(int width)
{
    return qMax(1, (width - 2 * padding + spacing) / (buttonSize + spacing));
}
}

struct KisTouchStudio::Private
{
    explicit Private(KisViewManager *viewManager, QWidget *parent)
        : manager(viewManager)
        , host(parent)
    {
    }

    KisViewManager *const manager;
    QPointer<QWidget> host;
    QPointer<QWidget> topBar;
    QPointer<QWidget> toolBar;
    QPointer<QWidget> brushBar;
    QPointer<KisTouchStudioPanels> panels;
    QPointer<KisTouchStudioAnimation> animation;

    QList<QToolButton *> topButtons;
    QList<QToolButton *> toolButtons;
    QList<QPair<QString, QToolButton *>> drawingTools;
    QList<QPair<QString, QToolButton *>> panelButtons;
    QToolButton *colorButton = nullptr;
    QToolButton *animationButton = nullptr;
    QGridLayout *topLayout = nullptr;
    QGridLayout *toolLayout = nullptr;
    QSlider *sizeSlider = nullptr;
    QSlider *opacitySlider = nullptr;
    QLabel *sizeValue = nullptr;
    QLabel *opacityValue = nullptr;

    QString activePanel;
    QMetaObject::Connection resourceConnection;
    int topColumns = 0;
    int toolColumns = 0;
    const qreal maxBrushSize = qMax<qreal>(1.0, KisImageConfig(true).maxBrushSize());
};

KisTouchStudio::KisTouchStudio(KisViewManager *manager, QWidget *host, QAction *exitAction)
    : QObject(host)
    , d(new Private(manager, host))
{
    d->topBar = makeBar(host, manager);
    d->topLayout = makeButtonGrid(d->topBar);
    d->toolBar = makeBar(host, manager);
    d->toolLayout = makeButtonGrid(d->toolBar);
    d->brushBar = makeBar(host, manager);

    const auto addTopAction = [this](const QString &text, const QString &icon, QAction *action) {
        auto *button = makeButton(d->topBar, text, icon);
        bindAction(button, action);
        d->topButtons.append(button);
    };
    auto *actions = manager->actionCollection();
    addTopAction(i18n("Undo"), QStringLiteral("edit-undo"), actions->action(QStringLiteral("edit_undo")));
    addTopAction(i18n("Redo"), QStringLiteral("edit-redo"), actions->action(QStringLiteral("edit_redo")));
    addTopAction(i18n("Save"), QStringLiteral("document-save-16"), actions->action(QStringLiteral("file_save")));

    const auto addPanelButton = [this](const QString &id, const QString &text, const QString &icon) {
        auto *button = makeButton(d->topBar, text, icon);
        button->setCheckable(true);
        d->topButtons.append(button);
        d->panelButtons.append(qMakePair(id, button));
        connect(button, &QToolButton::clicked, this, [this, id](bool checked) {
            d->activePanel = checked ? id : QString();
            for (const auto &pageButton : d->panelButtons) {
                pageButton.second->setChecked(pageButton.first == d->activePanel);
            }
            if (checked) {
                if (!d->panels) {
                    d->panels = new KisTouchStudioPanels(d->manager, d->host);
                    matchCanvasSurface(d->panels, d->manager);
                }
                d->panels->showPage(id);
                d->panels->show();
            } else if (d->panels) {
                d->panels->hide();
            }
            layoutOverlays();
        });
        return button;
    };

    addPanelButton(QStringLiteral("brushes"), i18n("Brush Presets"), QStringLiteral("preset-switcher"));
    d->colorButton = addPanelButton(QStringLiteral("color"), i18n("Foreground Color"), QStringLiteral("config-popup-palette"));
    addPanelButton(QStringLiteral("layers"), i18n("Layers"), QStringLiteral("all-layers"));
    d->animationButton = makeButton(d->topBar, i18n("Animation Timeline"), QStringLiteral("render-animation"));
    d->animationButton->setCheckable(true);
    d->topButtons.append(d->animationButton);
    connect(d->animationButton, &QToolButton::toggled, this, [this](bool visible) {
        if (visible && !d->animation) {
            d->animation = new KisTouchStudioAnimation(d->manager, d->host);
            matchCanvasSurface(d->animation, d->manager);
        }
        if (d->animation) {
            d->animation->setVisible(visible);
        }
        layoutOverlays();
    });
    auto *exitButton = makeButton(d->topBar, i18n("Exit Tablet Mode"), QStringLiteral("window-close"));
    d->topButtons.append(exitButton);
    if (exitAction) {
        // Exiting can destroy this button. Trigger the action after its input event returns.
        connect(exitButton, &QToolButton::clicked, this, [action = QPointer<QAction>(exitAction)] {
            if (action) {
                action->trigger();
            }
        }, Qt::QueuedConnection);
    } else {
        exitButton->setEnabled(false);
    }

    const auto addTool = [this](const QString &id, const QString &text, const QString &icon) {
        auto *button = makeButton(d->toolBar, text, icon);
        button->setCheckable(true);
        d->toolButtons.append(button);
        d->drawingTools.append(qMakePair(id, button));
        connect(button, &QToolButton::clicked, this, [this, id] {
            d->manager->toolManager()->switchToolRequested(id);
            updateTools();
        });
    };
    addTool(QStringLiteral("KritaShape/KisToolBrush"), i18n("Freehand Brush"), QStringLiteral("krita_tool_freehand"));

    auto *eraser = makeButton(d->toolBar, i18n("Eraser Mode"), QStringLiteral("draw-eraser"));
    bindAction(eraser, actions->action(QStringLiteral("erase_action")));
    d->toolButtons.append(eraser);

    addTool(QStringLiteral("KritaSelected/KisToolColorSampler"), i18n("Sample Color"), QStringLiteral("krita_tool_color_sampler"));
    addTool(QStringLiteral("KritaShape/KisToolLine"), i18n("Line"), QStringLiteral("krita_tool_line"));
    addTool(QStringLiteral("KritaFill/KisToolFill"), i18n("Fill"), QStringLiteral("krita_tool_color_fill"));
    addTool(QStringLiteral("KisToolSelectRectangular"), i18n("Rectangular Selection"), QStringLiteral("tool_rect_selection"));
    addTool(QStringLiteral("KisToolTransform"), i18n("Transform"), QStringLiteral("krita_tool_transform"));

    auto *brushControls = new QBoxLayout(QBoxLayout::LeftToRight, d->brushBar);
    brushControls->setContentsMargins(padding, padding, padding, padding);
    brushControls->setSpacing(padding);
    const auto addBrushControl = [this, brushControls](const QString &label, QLabel *&valueLabel) {
        auto *column = new QVBoxLayout();
        column->setSpacing(spacing);
        auto *title = new QLabel(label, d->brushBar);
        title->setAlignment(Qt::AlignCenter);
        column->addWidget(title);
        auto *slider = new TouchSlider(d->brushBar);
        slider->setAccessibleName(label);
        slider->setToolTip(label);
        column->addWidget(slider, 0, Qt::AlignCenter);
        valueLabel = new QLabel(d->brushBar);
        valueLabel->setAlignment(Qt::AlignCenter);
        column->addWidget(valueLabel);
        brushControls->addLayout(column);
        return slider;
    };

    d->sizeSlider = addBrushControl(i18n("Size"), d->sizeValue);
    d->sizeValue->setMinimumWidth(d->sizeValue->fontMetrics().horizontalAdvance(
        i18n("%1 px", QLocale().toString(qMax<qreal>(99.99, d->maxBrushSize), 'f', 2))));
    d->sizeSlider->setRange(0, 1000);
    d->sizeSlider->setPageStep(50);
    connect(d->sizeSlider, &QSlider::valueChanged, this, [this](int position) {
        auto *provider = d->manager->canvasResourceProvider();
        if (provider->resourceManager() && d->manager->canvasBase()) {
            // Give small brushes more slider travel while preserving the full configured size range.
            provider->setSize(0.01 * std::pow(d->maxBrushSize / 0.01, position / 1000.0));
        }
    });

    d->opacitySlider = addBrushControl(i18n("Opacity"), d->opacityValue);
    d->opacityValue->setMinimumWidth(d->opacityValue->fontMetrics().horizontalAdvance(i18n("%1%", 100)));
    d->opacitySlider->setRange(0, 100);
    d->opacitySlider->setPageStep(10);
    connect(d->opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        auto *provider = d->manager->canvasResourceProvider();
        if (provider->resourceManager() && d->manager->canvasBase()) {
            provider->setOpacity(value / 100.0);
        }
    });

    connect(manager->toolManager(), &KoToolManager::changedTool, this, &KisTouchStudio::updateTools);
    connect(manager, &KisViewManager::viewChanged, this, &KisTouchStudio::bindCanvasResources);
    host->installEventFilter(this);
    bindCanvasResources();
    layoutOverlays();
    d->topBar->show();
    d->toolBar->show();
    d->brushBar->show();
}

KisTouchStudio::~KisTouchStudio()
{
    if (d->host) {
        d->host->removeEventFilter(this);
    }
    delete d->animation;
    delete d->panels;
    delete d->brushBar;
    delete d->toolBar;
    delete d->topBar;
}

bool KisTouchStudio::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->host && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        layoutOverlays();
    }
    return QObject::eventFilter(watched, event);
}

void KisTouchStudio::layoutOverlays()
{
    const QRect area = d->host->rect().adjusted(edge, edge, -edge, -edge);
    const int topColumns = qMin<int>(d->topButtons.size(), buttonsPerRow(area.width()));
    if (topColumns != d->topColumns) {
        arrangeButtons(d->topLayout, d->topButtons, topColumns);
        d->topColumns = topColumns;
    }
    d->topBar->adjustSize();
    d->topBar->move(area.center().x() - d->topBar->width() / 2, area.top());

    int bottom = area.bottom();
    if (d->animation && !d->animation->isHidden()) {
        const int height = qBound(88, area.height() / 4, 108);
        d->animation->setGeometry(area.left(), bottom - height + 1, area.width(), height);
        bottom = d->animation->y() - gap;
        d->animation->raise();
    }

    const int top = d->topBar->geometry().bottom() + gap;
    const int railHeight = 2 * padding + d->toolButtons.size() * (buttonSize + spacing) - spacing;
    const bool verticalRail = bottom - top >= railHeight;
    const int toolColumns = verticalRail ? 1 : qMin<int>(d->toolButtons.size(), buttonsPerRow(area.width()));
    if (toolColumns != d->toolColumns) {
        arrangeButtons(d->toolLayout, d->toolButtons, toolColumns);
        d->toolColumns = toolColumns;
    }
    d->toolBar->adjustSize();
    const int toolY = verticalRail ? top + (bottom - top - d->toolBar->height()) / 2 : top;
    d->toolBar->move(area.left(), toolY);

    const int contentTop = verticalRail ? top : d->toolBar->geometry().bottom() + gap;
    const bool verticalSliders = bottom - contentTop >= 158;
    for (QSlider *slider : {d->sizeSlider, d->opacitySlider}) {
        slider->setOrientation(verticalSliders ? Qt::Vertical : Qt::Horizontal);
        if (verticalSliders) {
            slider->setFixedSize(buttonSize, qBound(104, bottom - contentTop - 54, 164));
        } else {
            slider->setFixedSize(qBound(44, area.width() / 4, 120), buttonSize);
        }
    }
    d->brushBar->adjustSize();
    const int brushY = verticalSliders ? contentTop + (bottom - contentTop - d->brushBar->height()) / 2
                                       : qMax(contentTop, bottom - d->brushBar->height() + 1);
    d->brushBar->move(area.right() - d->brushBar->width() + 1, brushY);

    d->toolBar->raise();
    d->brushBar->raise();
    if (d->panels && !d->panels->isHidden()) {
        const QSize minimum = d->panels->minimumSizeHint();
        int right = verticalSliders ? d->brushBar->x() - gap : area.right();
        int left = verticalRail ? d->toolBar->geometry().right() + gap : area.left();
        int panelTop = contentTop;
        int panelBottom = verticalSliders ? bottom : d->brushBar->y() - gap;
        if (right - left + 1 < minimum.width()) {
            left = area.left();
            right = area.right();
        }
        if (panelBottom - panelTop + 1 < minimum.height()) {
            panelTop = top;
            panelBottom = bottom;
        }
        if (panelBottom - panelTop + 1 < minimum.height()) {
            panelBottom = area.bottom();
        }
        const int availableWidth = qMax(1, right - left + 1);
        const int availableHeight = qMax(1, panelBottom - panelTop + 1);
        const QSize hint = d->panels->sizeHint().expandedTo(QSize(280, 200));
        const QSize size(qMin(hint.width(), availableWidth), qMin(hint.height(), availableHeight));
        d->panels->setGeometry(right - size.width() + 1, panelTop, size.width(), size.height());
        d->panels->raise();
    }
    d->topBar->raise();
}

void KisTouchStudio::updateTools()
{
    auto *toolManager = d->manager->toolManager();
    auto *canvas = d->manager->canvasBase();
    const QString activeId = toolManager->activeToolId();
    for (const auto &entry : d->drawingTools) {
        entry.second->setEnabled(canvas && toolManager->toolById(canvas, entry.first));
        entry.second->setChecked(entry.first == activeId);
    }
}

void KisTouchStudio::bindCanvasResources()
{
    disconnect(d->resourceConnection);
    auto *resources = d->manager->canvasResourceProvider()->resourceManager();
    if (resources) {
        d->resourceConnection = connect(resources, &KoCanvasResourceProvider::canvasResourceChanged,
                                        this, [this](int key, const QVariant &) {
            if (key == KoCanvasResource::Size || key == KoCanvasResource::Opacity || key == KoCanvasResource::ForegroundColor) {
                updateBrushControls();
            }
        });
    }
    updateTools();
    updateBrushControls();
}

void KisTouchStudio::updateBrushControls()
{
    auto *provider = d->manager->canvasResourceProvider();
    const bool hasCanvas = d->manager->canvasBase() && provider->resourceManager();
    d->brushBar->setEnabled(hasCanvas);
    for (const auto &pageButton : d->panelButtons) {
        pageButton.second->setEnabled(hasCanvas);
    }
    d->animationButton->setEnabled(hasCanvas);
    if (!hasCanvas) {
        return;
    }

    const QSignalBlocker sizeBlocker(d->sizeSlider);
    const QSignalBlocker opacityBlocker(d->opacitySlider);
    const qreal size = qBound<qreal>(0.01, provider->size(), d->maxBrushSize);
    const int position = qRound(1000 * std::log(size / 0.01) / std::log(d->maxBrushSize / 0.01));
    const int opacity = qRound(provider->opacity() * 100);
    d->sizeSlider->setValue(position);
    d->opacitySlider->setValue(opacity);
    d->sizeValue->setText(i18n("%1 px", QLocale().toString(size, 'f', size < 10 ? 2 : 0)));
    d->opacityValue->setText(i18n("%1%", opacity));

    QPixmap swatch(44, 44);
    swatch.setDevicePixelRatio(2);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(d->manager->canvasBase()->displayRendererInterface()->toQColor(provider->fgColor()));
    painter.setPen(QColor(QStringLiteral("#d5dce8")));
    painter.drawEllipse(QRectF(2, 2, 18, 18));
    painter.end();
    d->colorButton->setIcon(QIcon(swatch));
}
