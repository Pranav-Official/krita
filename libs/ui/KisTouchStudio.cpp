/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudio.h"

#include <QAction>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QMenu>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QPainterPath>
#include <QScroller>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <kactioncollection.h>
#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>
#include <KoColorDisplayRendererInterface.h>
#include <KoToolManager.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "KisTouchStudioAnimation.h"
#include "KisTouchStudioContext.h"
#include "KisTouchStudioPanels.h"
#include "KisViewManager.h"
#include "KisView.h"
#include "canvas/kis_canvas2.h"
#include "kis_canvas_resource_provider.h"
#include "kis_config.h"
#include "kis_icon_utils.h"
#include "kis_image_config.h"

namespace
{
constexpr int edge = 10;
constexpr int gap = 8;
constexpr int buttonSize = 44;
constexpr int padding = 6;
constexpr int spacing = 2;
constexpr int topHeight = 52;

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

QWidget *makeSurface(QWidget *parent, KisViewManager *manager, const QString &name)
{
    auto *surface = new QWidget(parent);
    matchCanvasSurface(surface, manager);
    surface->setObjectName(name);
    surface->setAttribute(Qt::WA_NoMousePropagation);
    surface->setAttribute(Qt::WA_StyledBackground);
    surface->setStyleSheet(QStringLiteral(R"(
        QWidget#touchStudioBar { background: #25262a; }
        QWidget#touchStudioRail, QWidget#touchStudioCommands {
            background: #25262a;
            border-radius: 12px;
        }
        QToolButton {
            background: transparent;
            color: #efeff1;
            border: 1px solid transparent;
            border-radius: 7px;
            font-weight: 600;
        }
        QToolButton:hover { background: #303137; }
        QToolButton:checked { background: #3c3e45; }
        QToolButton[studioTool="true"]:checked { background: transparent; }
        QToolButton[studioTool="true"]:checked:hover { background: #303137; }
        QToolButton:pressed, QToolButton[studioTool="true"]:checked:pressed { background: #45474f; }
        QToolButton:focus { border-color: #88baff; }
        QToolButton:disabled { color: #85858d; }
        QLabel { color: #efeff1; }
        QLabel#brushReadout { color: #d0d0d6; font-size: 11px; }
        QLabel#studioCurrentTool { color: #b6b6be; }
        QSlider::groove:vertical { background: #17181b; width: 5px; border-radius: 2px; }
        QSlider::groove:horizontal { background: #17181b; height: 5px; border-radius: 2px; }
        QSlider::handle { background: #efeff1; border: 1px solid transparent; border-radius: 6px; }
        QSlider::handle:vertical { height: 12px; margin: 0 -5px; }
        QSlider::handle:horizontal { width: 12px; margin: -5px 0; }
        QSlider::handle:pressed { background: #88baff; }
        QSlider::handle:focus { border-color: #88baff; }
        QLineEdit {
            min-height: 42px; padding: 0 11px; background: #1b1c20; color: #efeff1;
            border: 1px solid transparent; border-radius: 8px;
        }
        QLineEdit:focus { border-color: #88baff; }
        QTreeWidget {
            background: transparent; border: 0;
            color: #efeff1; selection-background-color: #3c3e45; selection-color: #efeff1;
        }
        QTreeWidget::item { border-radius: 5px; }
        QTreeWidget::item:hover:!selected { background: #303137; }
        QTreeWidget::item:disabled { color: #85858d; }
    )"));
    return surface;
}

enum class Glyph { Documents, Actions, Tools, Studio, Options, Animation, Brush, Erase,
                   Presets, Layers, Undo, Redo, Size, Opacity };

QPixmap studioGlyph(Glyph glyph, const QColor &ink, int size = 24)
{
    QPixmap pixmap(size * 2, size * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    p.setPen(QPen(ink, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto polyline = [&p](std::initializer_list<QPointF> points) {
        p.drawPolyline(QPolygonF(points));
    };

    switch (glyph) {
    case Glyph::Documents:
        polyline({{5, 6}, {12, 6}, {16, 10}, {16, 20}, {5, 20}, {5, 6}});
        polyline({{12, 6}, {12, 10}, {16, 10}});
        polyline({{9, 3}, {17, 3}, {20, 6}, {20, 16}});
        break;
    case Glyph::Actions:
        for (const qreal y : {6.0, 12.0, 18.0}) {
            line(9, y, 20, y);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        for (const qreal y : {6.0, 12.0, 18.0}) {
            p.drawEllipse(QPointF(5, y), 1.25, 1.25);
        }
        break;
    case Glyph::Tools:
        p.drawRoundedRect(QRectF(3.5, 8, 17, 11), 2, 2);
        polyline({{8.5, 8}, {8.5, 5}, {15.5, 5}, {15.5, 8}});
        line(3.5, 12, 20.5, 12);
        line(10.5, 13.5, 13.5, 13.5);
        break;
    case Glyph::Studio:
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        line(9, 5, 9, 19);
        line(9, 12, 21, 12);
        break;
    case Glyph::Options:
        line(4, 7, 20, 7);
        line(4, 12, 20, 12);
        line(4, 17, 20, 17);
        p.setBrush(QColor(QStringLiteral("#25262a")));
        for (const QPointF &center : {QPointF(9, 7), QPointF(15, 12), QPointF(11, 17)}) {
            p.drawEllipse(center, 2, 2);
        }
        break;
    case Glyph::Animation:
        p.drawRoundedRect(QRectF(3.5, 5, 17, 14), 2, 2);
        line(7, 5, 7, 19);
        line(17, 5, 17, 19);
        polyline({{10, 8.5}, {15, 12}, {10, 15.5}, {10, 8.5}});
        break;
    case Glyph::Brush: {
        polyline({{9, 13}, {16.5, 4.5}, {19.5, 3.5}, {20.5, 5.5}, {12, 16}});
        QPainterPath bristles(QPointF(9, 13));
        bristles.cubicTo(5, 13, 6.5, 17, 3.5, 20);
        bristles.cubicTo(7.5, 21, 12, 19, 12, 16);
        bristles.closeSubpath();
        p.fillPath(bristles, ink);
        break;
    }
    case Glyph::Erase:
        polyline({{7, 18}, {3.5, 14.5}, {13.5, 4.5}, {20.5, 11.5}, {14, 18}, {7, 18}});
        line(8.5, 10, 15.5, 17);
        line(14, 18, 21, 18);
        break;
    case Glyph::Presets:
        for (int y : {5, 13}) {
            for (int x : {5, 13}) {
                p.drawRoundedRect(QRectF(x, y, 6, 6), 1.4, 1.4);
            }
        }
        break;
    case Glyph::Layers:
        polyline({{3.5, 9}, {12, 4.5}, {20.5, 9}, {12, 13.5}, {3.5, 9}});
        polyline({{3.5, 13}, {12, 17.5}, {20.5, 13}});
        polyline({{3.5, 17}, {12, 21.5}, {20.5, 17}});
        break;
    case Glyph::Redo:
        p.translate(24, 0);
        p.scale(-1, 1);
        [[fallthrough]];
    case Glyph::Undo: {
        polyline({{8, 6}, {4, 10}, {8, 14}});
        QPainterPath path(QPointF(4, 10));
        path.lineTo(14, 10);
        path.cubicTo(22, 10, 21, 19, 14, 19);
        p.drawPath(path);
        break;
    }
    case Glyph::Size:
        p.drawEllipse(QRectF(5, 5, 14, 14));
        p.drawEllipse(QPointF(12, 12), 2, 2);
        break;
    case Glyph::Opacity:
        p.drawEllipse(QRectF(5, 5, 14, 14));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawPie(QRectF(5, 5, 14, 14), 90 * 16, 180 * 16);
        break;
    }
    p.end();
    return pixmap;
}

QIcon studioIcon(Glyph glyph, bool active = false)
{
    return QIcon(studioGlyph(glyph, QColor(active ? QStringLiteral("#88baff") : QStringLiteral("#efeff1"))));
}

QIcon barIcon(const QString &name)
{
    const QString lightPath = QStringLiteral(":/light_%1.svg").arg(name);
    if (QFile::exists(lightPath)) {
        return QIcon(lightPath);
    }
    return KisIconUtils::loadIcon(name);
}

QToolButton *makeButton(QWidget *parent, const QString &text, const QIcon &icon = QIcon())
{
    auto *button = new QToolButton(parent);
    button->setFixedSize(buttonSize, buttonSize);
    button->setIconSize(QSize(22, 22));
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setFocusPolicy(Qt::TabFocus);
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

void addToolbarSeparator(QBoxLayout *layout)
{
    auto *line = new QWidget(layout->parentWidget());
    line->setFixedSize(1, 23);
    line->setStyleSheet(QStringLiteral("background: #414248;"));
    layout->addSpacing(3);
    layout->addWidget(line);
    layout->addSpacing(3);
}

QString plainActionText(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QString commandGroup(const QString &id)
{
    if (id.startsWith(QLatin1String("file_")) || id.contains(QLatin1String("export"))) {
        return i18n("Document");
    }
    if (id.startsWith(QLatin1String("krita_filter_")) || id.startsWith(QLatin1String("filter_"))) {
        return i18n("Filters");
    }
    if (id.contains(QLatin1String("selection")) || id.contains(QLatin1String("select"))) {
        return i18n("Selection");
    }
    if (id.contains(QLatin1String("transform")) || id.contains(QLatin1String("mirror"))
        || id.contains(QLatin1String("rotate")) || id.contains(QLatin1String("resize"))
        || id.contains(QLatin1String("scale")) || id.contains(QLatin1String("crop"))) {
        return i18n("Transform");
    }
    if (id.contains(QLatin1String("layer")) || id.contains(QLatin1String("mask"))) {
        return i18n("Layers");
    }
    if (id.contains(QLatin1String("frame")) || id.contains(QLatin1String("animation"))) {
        return i18n("Animation");
    }
    if (id.startsWith(QLatin1String("edit_")) || id.contains(QLatin1String("clipboard"))
        || id.startsWith(QLatin1String("paste")) || id.startsWith(QLatin1String("copy"))
        || id.startsWith(QLatin1String("fill_"))) {
        return i18n("Edit");
    }
    if (id.startsWith(QLatin1String("zoom_")) || id.startsWith(QLatin1String("view_"))
        || id.contains(QLatin1String("canvas"))) {
        return i18n("Canvas");
    }
    return i18n("Other Commands");
}

QString documentTitle(const KisDocument *document)
{
    const QString name = QFileInfo(document->path()).fileName();
    return name.isEmpty() ? document->caption() : name;
}

constexpr int entryRole = Qt::UserRole;

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
    QPointer<QWidget> brushBar;
    QPointer<QWidget> commands;
    QPointer<KisTouchStudioPanels> panels;
    QPointer<KisTouchStudioAnimation> animation;
    QPointer<KisTouchStudioContext> context;

    QList<QPair<QString, QToolButton *>> panelButtons;
    QToolButton *brushButton = nullptr;
    QToolButton *eraserButton = nullptr;
    QToolButton *colorButton = nullptr;
    QToolButton *animationButton = nullptr;
    QToolButton *documentsButton = nullptr;
    QToolButton *actionsButton = nullptr;
    QToolButton *toolsButton = nullptr;
    QToolButton *studioButton = nullptr;
    QToolButton *optionsButton = nullptr;
    QLabel *activeTool = nullptr;
    QWidget *leftToolbar = nullptr;
    QWidget *rightToolbar = nullptr;
    QBoxLayout *topLayout = nullptr;
    QBoxLayout *brushLayout = nullptr;
    QBoxLayout *historyLayout = nullptr;
    QLineEdit *search = nullptr;
    QTreeWidget *results = nullptr;
    QSlider *sizeSlider = nullptr;
    QSlider *opacitySlider = nullptr;
    QLabel *sizeValue = nullptr;
    QLabel *opacityValue = nullptr;

    struct Entry {
        QString category;
        QString title;
        QString id;
        QIcon icon;
        QPointer<QAction> action;
        QPointer<KoToolAction> tool;
        QString panel;
        QPointer<KisView> view {};
    };
    QList<Entry> entries;
    QStringList recentEntries = KisConfig(true).readEntry<QStringList>(QStringLiteral("tabletStudioRecentCommands"));
    QString activePanel;
    QMetaObject::Connection resourceConnection;
    QMetaObject::Connection documentConnection;
    const qreal maxBrushSize = qMax<qreal>(1.0, KisImageConfig(true).maxBrushSize());
};

KisTouchStudio::KisTouchStudio(KisViewManager *manager, QWidget *host, QAction *exitAction)
    : QObject(host)
    , d(new Private(manager, host))
{
    d->topBar = makeSurface(host, manager, QStringLiteral("touchStudioBar"));
    d->brushBar = makeSurface(host, manager, QStringLiteral("touchStudioRail"));

    d->topLayout = new QBoxLayout(QBoxLayout::LeftToRight, d->topBar);
    d->topLayout->setContentsMargins(8, 4, 8, 4);
    d->topLayout->setSpacing(8);
    d->leftToolbar = new QWidget(d->topBar);
    auto *left = new QHBoxLayout(d->leftToolbar);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(spacing);
    d->rightToolbar = new QWidget(d->topBar);
    auto *right = new QHBoxLayout(d->rightToolbar);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(spacing);

    d->topLayout->addWidget(d->leftToolbar);
    d->topLayout->addStretch();
    d->topLayout->addWidget(d->rightToolbar);

    auto *exitButton = makeButton(d->leftToolbar, i18n("Exit Tablet Mode"));
    exitButton->setText(i18n("Done"));
    exitButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    exitButton->setFixedWidth(qMax(64, exitButton->fontMetrics().horizontalAdvance(exitButton->text()) + 18));
    left->addWidget(exitButton);
    exitButton->setEnabled(exitAction);
    if (exitAction) {
        connect(exitButton, &QToolButton::clicked, this, [action = QPointer<QAction>(exitAction)] {
            if (action) {
                action->trigger();
            }
        }, Qt::QueuedConnection);
    }

    d->documentsButton = makeButton(d->leftToolbar, i18n("Open Documents"), studioIcon(Glyph::Documents));
    d->documentsButton->setCheckable(true);
    left->addWidget(d->documentsButton);
    addToolbarSeparator(left);
    connect(d->documentsButton, &QToolButton::clicked, this, [this] { showCommands(QStringLiteral("documents")); });

    d->actionsButton = makeButton(d->leftToolbar, i18n("Actions"), studioIcon(Glyph::Actions));
    d->actionsButton->setCheckable(true);
    left->addWidget(d->actionsButton);
    connect(d->actionsButton, &QToolButton::clicked, this, [this] { showCommands(QStringLiteral("actions")); });

    d->animationButton = makeButton(d->leftToolbar, i18n("Animation Timeline"), studioIcon(Glyph::Animation));
    d->animationButton->setCheckable(true);
    left->addWidget(d->animationButton);
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

    d->activeTool = new QLabel(d->leftToolbar);
    d->activeTool->setObjectName(QStringLiteral("studioCurrentTool"));
    d->activeTool->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    d->activeTool->setFixedWidth(148);
    d->activeTool->setMargin(6);
    left->addWidget(d->activeTool);

    d->brushButton = makeButton(d->rightToolbar, i18n("Freehand Brush"), studioIcon(Glyph::Brush));
    d->brushButton->setCheckable(true);
    d->brushButton->setProperty("studioTool", true);
    right->addWidget(d->brushButton);
    connect(d->brushButton, &QToolButton::clicked, this, [this] {
        d->manager->toolManager()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
        updateTools();
    });

    auto *actions = manager->actionCollection();
    d->eraserButton = makeButton(d->rightToolbar, i18n("Eraser Mode"), studioIcon(Glyph::Erase));
    d->eraserButton->setProperty("studioTool", true);
    auto *eraseAction = actions->action(QStringLiteral("erase_action"));
    bindAction(d->eraserButton, eraseAction);
    right->addWidget(d->eraserButton);

    d->toolsButton = makeButton(d->rightToolbar, i18n("Tools"), studioIcon(Glyph::Tools));
    d->toolsButton->setCheckable(true);
    right->addWidget(d->toolsButton);
    connect(d->toolsButton, &QToolButton::clicked, this, [this] { showCommands(QStringLiteral("tools")); });
    if (eraseAction) {
        connect(eraseAction, &QAction::toggled, this, [this](bool active) { updateToolIcons(active); });
    }

    const auto addPanelButton = [this, right](const QString &id, const QString &text, const QIcon &icon) {
        auto *button = makeButton(d->rightToolbar, text, icon);
        button->setCheckable(true);
        right->addWidget(button);
        d->panelButtons.append(qMakePair(id, button));
        connect(button, &QToolButton::clicked, this, [this, id] { setPanel(d->activePanel == id ? QString() : id); });
        return button;
    };

    d->optionsButton = addPanelButton(QStringLiteral("options"), i18n("Tool Options"), studioIcon(Glyph::Options));
    addToolbarSeparator(right);
    addPanelButton(QStringLiteral("brushes"), i18n("Brush Presets"), studioIcon(Glyph::Presets));
    d->colorButton = addPanelButton(QStringLiteral("color"), i18n("Foreground Color"), QIcon());
    d->colorButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addPanelButton(QStringLiteral("layers"), i18n("Layers"), studioIcon(Glyph::Layers));
    d->studioButton = addPanelButton(QStringLiteral("studio"), i18n("Studio Panels"), studioIcon(Glyph::Studio));

    d->brushLayout = new QBoxLayout(QBoxLayout::TopToBottom, d->brushBar);
    d->brushLayout->setContentsMargins(padding, padding, padding, padding);
    d->brushLayout->setSpacing(7);
    auto *brushControls = d->brushLayout;
    const auto addBrushControl = [this, brushControls](const QString &label, Glyph glyph, QLabel *&valueLabel) {
        auto *column = new QVBoxLayout();
        column->setSpacing(spacing);
        auto *symbol = new QLabel(d->brushBar);
        symbol->setFixedSize(14, 14);
        symbol->setPixmap(studioGlyph(glyph, QColor(QStringLiteral("#d0d0d6")), symbol->width()));
        column->addWidget(symbol, 0, Qt::AlignHCenter);
        auto *slider = new TouchSlider(d->brushBar);
        slider->setAccessibleName(label);
        slider->setToolTip(label);
        column->addWidget(slider, 0, Qt::AlignCenter);
        valueLabel = new QLabel(d->brushBar);
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setObjectName(QStringLiteral("brushReadout"));
        column->addWidget(valueLabel);
        brushControls->addLayout(column);
        return slider;
    };

    d->sizeSlider = addBrushControl(i18n("Size"), Glyph::Size, d->sizeValue);
    d->sizeValue->setMinimumWidth(d->sizeValue->fontMetrics().horizontalAdvance(
        i18n("%1 px", QLocale().toString(qMax<qreal>(1000, d->maxBrushSize), 'f', 0))));
    d->sizeSlider->setRange(0, 1000);
    d->sizeSlider->setPageStep(50);
    connect(d->sizeSlider, &QSlider::valueChanged, this, [this](int position) {
        auto *provider = d->manager->canvasResourceProvider();
        if (provider->resourceManager() && d->manager->canvasBase()) {
            // Give small brushes more slider travel while preserving the full configured size range.
            provider->setSize(0.01 * std::pow(d->maxBrushSize / 0.01, position / 1000.0));
        }
    });

    d->opacitySlider = addBrushControl(i18n("Opacity"), Glyph::Opacity, d->opacityValue);
    d->opacityValue->setMinimumWidth(d->opacityValue->fontMetrics().horizontalAdvance(i18n("%1%", 100)));
    d->opacitySlider->setRange(0, 100);
    d->opacitySlider->setPageStep(10);
    connect(d->opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        auto *provider = d->manager->canvasResourceProvider();
        if (provider->resourceManager() && d->manager->canvasBase()) {
            provider->setOpacity(value / 100.0);
        }
    });

    d->historyLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    d->historyLayout->setSpacing(spacing);
    auto *undo = makeButton(d->brushBar, i18n("Undo"), studioIcon(Glyph::Undo));
    auto *redo = makeButton(d->brushBar, i18n("Redo"), studioIcon(Glyph::Redo));
    bindAction(undo, actions->action(QStringLiteral("edit_undo")));
    bindAction(redo, actions->action(QStringLiteral("edit_redo")));
    d->historyLayout->addWidget(undo);
    d->historyLayout->addWidget(redo);
    d->brushLayout->addLayout(d->historyLayout);

    d->context = new KisTouchStudioContext(manager, host);
    matchCanvasSurface(d->context, manager);

    connect(manager->toolManager(), &KoToolManager::changedTool, this, &KisTouchStudio::updateTools);
    connect(manager, &KisViewManager::viewChanged, this, &KisTouchStudio::bindCanvasResources);
    host->installEventFilter(this);
    bindCanvasResources();
    layoutOverlays();
    d->topBar->show();
    d->brushBar->show();
}

KisTouchStudio::~KisTouchStudio()
{
    if (d->host) {
        d->host->removeEventFilter(this);
    }
    delete d->context;
    delete d->animation;
    delete d->panels;
    delete d->brushBar;
    delete d->commands;
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
    const QRect host = d->host->rect();
    const bool twoRows = host.width() < 624;
    const bool compact = host.width() < 410;
    d->activeTool->setVisible(host.width() >= 940);
    d->documentsButton->setToolButtonStyle(host.width() >= 760 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    d->documentsButton->setFixedWidth(host.width() >= 760 ? 157 : buttonSize);
    d->studioButton->setVisible(!compact);
    d->optionsButton->setVisible(!compact);
    d->topLayout->setDirection(twoRows ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    d->topLayout->setSpacing(twoRows ? 2 : 8);
    d->topLayout->setAlignment(d->leftToolbar, twoRows ? Qt::AlignLeft : Qt::AlignVCenter);
    d->topLayout->setAlignment(d->rightToolbar, twoRows ? Qt::AlignRight : Qt::AlignVCenter);
    const int headerHeight = twoRows ? 2 * topHeight - 4 : topHeight;
    d->topBar->setGeometry(host.x(), host.y(), host.width(), headerHeight);

    const QRect area = host.adjusted(edge, headerHeight + edge, -edge, -edge);
    int bottom = area.bottom();
    if (d->animation && !d->animation->isHidden()) {
        const int height = qBound(96, area.height() / 3, d->animation->sizeHint().height());
        const int width = qMin(1000, area.width());
        d->animation->setGeometry(area.center().x() - width / 2, bottom - height + 1, width, height);
        bottom = d->animation->y() - gap;
        d->animation->raise();
    }

    if (d->context && !d->context->isHidden()) {
        const QSize hint = d->context->sizeHint();
        const int width = qMin(area.width(), hint.width());
        d->context->setGeometry(area.center().x() - width / 2, bottom - hint.height() + 1, width, hint.height());
        bottom = d->context->y() - gap;
        d->context->raise();
    }

    const int availableHeight = bottom - area.top() + 1;
    const bool horizontalRail = availableHeight < 300;
    d->brushLayout->setDirection(horizontalRail ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    d->historyLayout->setDirection(horizontalRail ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    const int trackLength = qBound(62, (availableHeight - 171) / 2, 116);
    for (QSlider *slider : {d->sizeSlider, d->opacitySlider}) {
        slider->setOrientation(horizontalRail ? Qt::Horizontal : Qt::Vertical);
        slider->setFixedSize(horizontalRail ? QSize(52, 36) : QSize(44, trackLength));
    }
    d->brushBar->adjustSize();
    const int railY = horizontalRail ? qMax(area.top(), bottom - d->brushBar->height() + 1)
        : area.top() + qMax(0, (availableHeight - d->brushBar->height()) / 2);
    d->brushBar->move(area.left(), railY);
    d->brushBar->raise();

    QWidget *open = nullptr;
    if (d->commands && !d->commands->isHidden()) {
        open = d->commands;
    } else if (d->panels && !d->panels->isHidden()) {
        open = d->panels;
    }
    if (open) {
        const bool leftAligned = d->activePanel == QLatin1String("actions") || d->activePanel == QLatin1String("documents");
        const int width = qMax(1, qMin(416, area.width()));
        const int height = qMax(1, qMin(availableHeight < 210 ? area.height() : availableHeight,
                                       open == d->commands ? 540 : open->sizeHint().height()));
        open->setGeometry(leftAligned ? area.left() : area.right() - width + 1, area.top(), width, height);
        open->raise();
    }
    d->topBar->raise();
}

void KisTouchStudio::setPanel(const QString &id)
{
    d->activePanel = id;
    const bool commands = id == QLatin1String("tools") || id == QLatin1String("actions")
        || id == QLatin1String("documents");
    d->documentsButton->setChecked(id == QLatin1String("documents"));
    d->actionsButton->setChecked(id == QLatin1String("actions"));
    d->toolsButton->setChecked(id == QLatin1String("tools"));
    for (const auto &entry : d->panelButtons) {
        entry.second->setChecked(entry.first == id);
    }
    if (d->commands) {
        d->commands->setVisible(commands);
    }
    if (d->panels) {
        d->panels->hide();
    }
    if (!commands && !id.isEmpty()) {
        if (!d->panels) {
            d->panels = new KisTouchStudioPanels(d->manager, d->host);
            matchCanvasSurface(d->panels, d->manager);
        }
        d->panels->showPage(id);
        d->panels->show();
    }
    layoutOverlays();
}

void KisTouchStudio::showCommands(const QString &page)
{
    if (page == d->activePanel) {
        setPanel(QString());
        return;
    }
    if (!d->commands) {
        d->commands = makeSurface(d->host, d->manager, QStringLiteral("touchStudioCommands"));
        auto *layout = new QVBoxLayout(d->commands);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        d->search = new QLineEdit(d->commands);
        d->search->setObjectName(QStringLiteral("touchStudioSearch"));
        d->search->setAccessibleName(i18n("Search tools and commands"));
        d->search->setPlaceholderText(i18n("Search tools and commands"));
        d->search->setClearButtonEnabled(true);
        layout->addWidget(d->search);
        d->results = new QTreeWidget(d->commands);
        d->results->setObjectName(QStringLiteral("touchStudioResults"));
        d->results->setAccessibleName(i18n("Tools and commands"));
        d->results->setHeaderHidden(true);
        d->results->setIconSize(QSize(20, 20));
        d->results->setIndentation(13);
        d->results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        d->results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        d->results->setTextElideMode(Qt::ElideRight);
        d->results->setExpandsOnDoubleClick(false);
        QScroller::grabGesture(d->results->viewport(), QScroller::TouchGesture);
        layout->addWidget(d->results);
        connect(d->search, &QLineEdit::textChanged, this, &KisTouchStudio::populateCommands);
        const auto activate = [this](QTreeWidgetItem *item) {
            if (!item || d->commands->isHidden() || item->isDisabled()) {
                return;
            }
            const QVariant index = item->data(0, entryRole);
            if (!index.isValid()) {
                item->setExpanded(!item->isExpanded());
                return;
            }
            const Private::Entry entry = d->entries.at(index.toInt());
            if (!entry.panel.isEmpty()) {
                setPanel(entry.panel);
                return;
            }
            if (entry.tool || entry.action) {
                const QString key = (entry.tool ? QStringLiteral("tool:") : QStringLiteral("action:")) + entry.id;
                d->recentEntries.removeAll(key);
                d->recentEntries.prepend(key);
                if (d->recentEntries.size() > 8) {
                    d->recentEntries.removeLast();
                }
                KisConfig(false).writeEntry(QStringLiteral("tabletStudioRecentCommands"), d->recentEntries);
            }
            setPanel(QString());
            if (entry.view) {
                QMetaObject::invokeMethod(this, [view = entry.view] {
                    if (view) {
                        auto *window = qobject_cast<QMdiSubWindow *>(view->parentWidget());
                        if (window && window->mdiArea()) {
                            window->mdiArea()->setActiveSubWindow(window);
                        }
                    }
                }, Qt::QueuedConnection);
            } else if (entry.tool && d->manager->canvasBase()) {
                entry.tool->trigger();
            } else if (entry.action && entry.action->isEnabled()) {
                // File and workspace actions can rebuild the view and destroy this shell.
                QMetaObject::invokeMethod(entry.action, [action = entry.action] {
                    if (action && action->isEnabled()) {
                        action->trigger();
                    }
                }, Qt::QueuedConnection);
            }
        };
        connect(d->results, &QTreeWidget::itemClicked, this, activate);
        connect(d->results, &QTreeWidget::itemActivated, this, activate);
        connect(d->search, &QLineEdit::returnPressed, this, [this, activate] {
            for (QTreeWidgetItemIterator it(d->results, QTreeWidgetItemIterator::Enabled); *it; ++it) {
                if ((*it)->parent()) {
                    activate(*it);
                    return;
                }
            }
        });
    }
    const QSignalBlocker block(d->search);
    d->search->clear();
    setPanel(page);
    collectCommands();
    populateCommands();
    d->results->setFocus(Qt::OtherFocusReason);
}

void KisTouchStudio::collectCommands()
{
    const auto *toolManager = d->manager->toolManager();
    auto *canvas = d->manager->canvasBase();
    d->entries.clear();

    const bool documentPicker = d->activePanel == QLatin1String("documents");
    if (documentPicker) {
        auto *window = d->manager->mainWindow();
        for (const auto &view : KisPart::instance()->views()) {
            if (view && view->mainWindow() == window && view->document()) {
                d->entries.append({i18n("Open Documents"), documentTitle(view->document()), view->document()->path(),
                                   studioIcon(Glyph::Documents), {}, {}, {}, view});
            }
        }
    }
    QSet<QString> toolIds;
    for (KoToolAction *tool : toolManager->toolActionList()) {
        const QString id = tool->id();
        const QString name = tool->toolName().isEmpty() ? tool->iconText() : tool->toolName();
        if (documentPicker || !canvas || !toolManager->toolById(canvas, id) || name.isEmpty()) {
            continue;
        }
        QString category = i18n("Drawing Tools");
        if (id.contains(QLatin1String("Select"), Qt::CaseInsensitive)
            && !id.contains(QLatin1String("KritaSelected/"))) {
            category = i18n("Selection Tools");
        } else if (id.contains(QLatin1String("Transform"), Qt::CaseInsensitive)
                   || id.contains(QLatin1String("Crop"), Qt::CaseInsensitive)) {
            category = i18n("Transform Tools");
        }
        toolIds.insert(id);
        d->entries.append({category, name, id, barIcon(tool->iconName()), {}, tool, {}});
    }
    if (!documentPicker) {
        d->entries.append({i18n("Studio"), i18n("Studio Panels"), QStringLiteral("dockers"),
                       studioIcon(Glyph::Studio), {}, {}, QStringLiteral("studio")});
        d->entries.append({i18n("Studio"), i18n("Tool Options"), QStringLiteral("settings"),
                           studioIcon(Glyph::Options), {}, {}, QStringLiteral("options")});
    }

    for (QAction *action : d->manager->actionCollection()->actions()) {
        const QString id = action->objectName();
        const QString name = plainActionText(action->text());
        if (name.isEmpty() || id.isEmpty() || toolIds.contains(id) || action->isSeparator() || action->menu()
            || !action->isVisible() || id == QLatin1String("view_tablet_mode")) {
            continue;
        }
        const QString category = commandGroup(id);
        if (!documentPicker || category == i18n("Document")) {
            d->entries.append({category, name, id, action->icon(), action, {}, {}});
        }
    }

    const bool toolsFirst = d->activePanel == QLatin1String("tools");
    std::stable_sort(d->entries.begin(), d->entries.end(), [toolsFirst](const Private::Entry &a, const Private::Entry &b) {
        const auto rank = [toolsFirst](const Private::Entry &entry) {
            if (entry.view) return -1;
            if (entry.tool) return toolsFirst ? 0 : 3;
            if (entry.category == i18n("Document")) return toolsFirst ? 2 : 0;
            if (!entry.panel.isEmpty()) return 1;
            if (entry.category == i18n("Other Commands")) return 5;
            return 4;
        };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        const int group = QString::localeAwareCompare(a.category, b.category);
        return group == 0 ? QString::localeAwareCompare(a.title, b.title) < 0 : group < 0;
    });
}

void KisTouchStudio::populateCommands()
{
    const bool toolsFirst = d->activePanel == QLatin1String("tools");
    const bool documentPicker = d->activePanel == QLatin1String("documents");
    const QStringList words = d->search->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    d->results->clear();
    const auto makeGroup = [this](const QString &name) {
        auto *group = new QTreeWidgetItem(d->results, {name});
        group->setSizeHint(0, QSize(0, 35));
        QFont font = group->font(0);
        font.setWeight(QFont::DemiBold);
        group->setFont(0, font);
        group->setForeground(0, QColor(QStringLiteral("#c0c0c8")));
        return group;
    };
    const auto makeEntry = [this](QTreeWidgetItem *group, int row) {
        const auto &entry = d->entries.at(row);
        auto *item = new QTreeWidgetItem(group, {entry.title});
        item->setSizeHint(0, QSize(0, buttonSize));
        item->setIcon(0, entry.icon);
        item->setToolTip(0, entry.title);
        item->setData(0, entryRole, row);
        item->setDisabled(entry.action && !entry.action->isEnabled());
        if (entry.view && entry.view == d->manager->mainWindow()->activeView()) {
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
    };
    if (!documentPicker && words.isEmpty()) {
        const QStringList quick = toolsFirst
            ? QStringList{QStringLiteral("KritaShape/KisToolBrush"), QStringLiteral("KritaSelected/KisToolColorSampler"),
                          QStringLiteral("KritaFill/KisToolFill"), QStringLiteral("KisToolSelectRectangular"),
                          QStringLiteral("KisToolTransform"), QStringLiteral("KritaShape/KisToolLine")}
            : QStringList{QStringLiteral("file_save"), QStringLiteral("file_save_as"), QStringLiteral("file_export_file"),
                          QStringLiteral("zoom_to_fit"), QStringLiteral("mirror_canvas"), QStringLiteral("show_brush_editor"),
                          QStringLiteral("deselect"), QStringLiteral("invert_selection")};
        const auto addHighlights = [this, &makeGroup, &makeEntry](const QString &title, const QStringList &keys, bool tools) {
            QTreeWidgetItem *highlights = nullptr;
            for (const QString &id : keys) {
                for (int row = 0; row < d->entries.size(); ++row) {
                    const auto &entry = d->entries.at(row);
                    if (entry.id != id || (tools ? entry.tool.isNull() : entry.action.isNull())) {
                        continue;
                    }
                    if (!highlights) {
                        highlights = makeGroup(title);
                        highlights->setExpanded(true);
                    }
                    makeEntry(highlights, row);
                    break;
                }
            }
        };
        QStringList recent;
        const QString prefix = toolsFirst ? QStringLiteral("tool:") : QStringLiteral("action:");
        for (const QString &key : d->recentEntries) {
            if (key.startsWith(prefix)) {
                recent.append(key.mid(prefix.size()));
            }
        }
        addHighlights(i18n("Recently Used"), recent.mid(0, 4), toolsFirst);
        addHighlights(toolsFirst ? i18n("Quick Tools") : i18n("Quick Actions"), quick, toolsFirst);
    }

    QTreeWidgetItem *group = nullptr;
    for (int row = 0; row < d->entries.size(); ++row) {
        const Private::Entry &entry = d->entries.at(row);
        const QString haystack = entry.title + QLatin1Char(' ') + entry.category + QLatin1Char(' ') + entry.id;
        if (!std::all_of(words.cbegin(), words.cend(), [&haystack](const QString &word) {
            return haystack.contains(word, Qt::CaseInsensitive);
        })) {
            continue;
        }
        if (!group || group->text(0) != entry.category) {
            group = makeGroup(entry.category);
            group->setExpanded(documentPicker || !words.isEmpty());
        }
        makeEntry(group, row);
    }
    if (!group) {
        auto *empty = new QTreeWidgetItem(d->results, {i18n("No matching tools or commands")});
        empty->setDisabled(true);
    }
}

void KisTouchStudio::updateTools()
{
    auto *toolManager = d->manager->toolManager();
    auto *canvas = d->manager->canvasBase();
    const QString active = toolManager->activeToolId();
    if (d->context) {
        d->context->updateForTool(active);
        layoutOverlays();
    }
    const QString brushId = QStringLiteral("KritaShape/KisToolBrush");
    d->brushButton->setEnabled(canvas && toolManager->toolById(canvas, brushId));
    d->brushButton->setChecked(active == brushId);
    updateToolIcons(d->eraserButton->isChecked());
    for (KoToolAction *tool : toolManager->toolActionList()) {
        if (tool->id() == active) {
            const QString name = tool->toolName().isEmpty() ? tool->iconText() : tool->toolName();
            d->activeTool->setText(d->activeTool->fontMetrics().elidedText(name, Qt::ElideRight, d->activeTool->width() - 12));
            d->activeTool->setToolTip(name);
            return;
        }
    }
    d->activeTool->clear();
}

void KisTouchStudio::updateToolIcons(bool erasing)
{
    const bool brush = d->brushButton->isChecked();
    d->brushButton->setIcon(studioIcon(Glyph::Brush, brush && !erasing));
    d->eraserButton->setIcon(studioIcon(Glyph::Erase, erasing));
    const bool hasTool = !d->manager->toolManager()->activeToolId().isEmpty();
    d->toolsButton->setIcon(studioIcon(Glyph::Tools, hasTool && !brush && !erasing));
}

void KisTouchStudio::bindCanvasResources()
{
    disconnect(d->documentConnection);
    if (const auto *view = qobject_cast<KisView *>(d->host.data()); view && view->document()) {
        const auto updateTitle = [this, document = view->document()] {
            const QString title = documentTitle(document);
            d->documentsButton->setText(d->documentsButton->fontMetrics().elidedText(title, Qt::ElideRight, 111));
            d->documentsButton->setToolTip(i18n("Open Documents — %1", title));
        };
        d->documentConnection = connect(view->document(), &KisDocument::sigPathChanged, this, updateTitle);
        updateTitle();
    }
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
