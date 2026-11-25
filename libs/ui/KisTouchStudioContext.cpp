/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioContext.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QPointer>
#include <QToolButton>
#include <kactioncollection.h>
#include <klocalizedstring.h>

#include <KoToolBase.h>
#include <KoToolManager.h>
#include <kis_image.h>
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "input/kis_tool_invocation_action.h"

KisTouchStudioContext::KisTouchStudioContext(KisViewManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_layout(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("tabletContextShelf"));
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    m_layout->setContentsMargins(6, 5, 6, 5);
    m_layout->setSpacing(3);
    setStyleSheet(QStringLiteral(R"(
        QWidget#tabletContextShelf { background: #25262a; border-radius: 12px; }
        QToolButton {
            background: transparent; border: 1px solid transparent; color: #efeff1;
            padding: 0 9px; border-radius: 7px;
        }
        QToolButton:hover { background: #303137; }
        QToolButton:pressed { background: #45474f; }
        QToolButton:focus { border-color: #88baff; }
        QToolButton:disabled { color: #85858d; }
        QToolButton#tabletApply:enabled { color: #88baff; font-weight: 600; }
        QToolButton::menu-indicator { image: none; }
        QMenu { background: #25262a; color: #efeff1; border: 1px solid #414248; padding: 4px; }
        QMenu::item { padding: 12px 20px; }
        QMenu::item:selected { background: #3c3e45; }
    )"));
    hide();
}

bool KisTouchStudioContext::updateForTool(const QString &id)
{
    if (id == m_toolId) return !isHidden();
    m_toolId = id;
    while (auto *item = m_layout->takeAt(0)) {
        // Applying a change can switch tools from inside the button's event handler.
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }

    auto button = [this](const QString &text) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setAccessibleName(text);
        b->setFocusPolicy(Qt::TabFocus);
        b->setMinimumSize(42, 42);
        m_layout->addWidget(b);
        b->show();
        return b;
    };
    auto menu = [&button](const QString &name) {
        auto *b = button(name);
        auto *popup = new QMenu(b);
        b->setMenu(popup);
        b->setPopupMode(QToolButton::InstantPopup);
        return popup;
    };
    auto command = [this](const char *name) { return m_manager->actionCollection()->action(QLatin1String(name)); };
    auto menuCommand = [&command](QMenu *popup, const char *id, const QString &text) {
        if (QAction *action = command(id)) {
            QAction *entry = popup->addAction(text, action, &QAction::trigger);
            QObject::connect(popup, &QMenu::aboutToShow, entry, [entry, action] { entry->setEnabled(action->isEnabled()); });
        }
    };
    auto commandButton = [&button, &command](const char *id, const QString &text) -> QToolButton * {
        auto *b = button(text);
        auto *action = command(id);
        b->setEnabled(action && action->isEnabled());
        if (action) {
            QObject::connect(b, &QToolButton::clicked, action, &QAction::trigger);
            QObject::connect(action, &QAction::changed, b, [action, b] { b->setEnabled(action->isEnabled()); });
        }
        return b;
    };

    const bool transform = id == QLatin1String("KisToolTransform");
    const bool crop = id == QLatin1String("KisToolCrop");
    if (transform) {
        auto *modes = menu(i18n("Transform mode"));
        menuCommand(modes, "KisToolTransformFree", i18n("Free Transform"));
        menuCommand(modes, "KisToolTransformPerspective", i18n("Perspective"));
        menuCommand(modes, "KisToolTransformWarp", i18n("Warp"));
        menuCommand(modes, "KisToolTransformCage", i18n("Cage"));
        menuCommand(modes, "KisToolTransformLiquify", i18n("Liquify"));
        menuCommand(modes, "KisToolTransformMesh", i18n("Mesh"));
    } else if (crop) {
        auto *ratios = menu(i18n("Aspect ratio"));
        auto *tool = m_manager->toolManager()->toolById(m_manager->canvasBase(), id);
        if (tool) {
            const auto ratio = [ratios, tool](const QString &label, double value) {
                ratios->addAction(label, tool, [tool, value] {
                    tool->setProperty("lockRatio", false);
                    if (value > 0) {
                        tool->setProperty("ratio", value);
                        tool->setProperty("lockRatio", true);
                    }
                });
            };
            ratio(i18n("Unconstrained"), 0);
            ratio(QStringLiteral("1 : 1"), 1);
            ratio(QStringLiteral("4 : 3"), 4.0 / 3);
            ratio(QStringLiteral("3 : 4"), 3.0 / 4);
            ratio(QStringLiteral("16 : 9"), 16.0 / 9);
            ratio(QStringLiteral("9 : 16"), 9.0 / 16);
        }
    } else if (id.startsWith(QLatin1String("KisToolSelect"))) {
        auto *combine = menu(i18n("Combine"));
        menuCommand(combine, "selection_tool_mode_replace", i18n("Replace Selection"));
        menuCommand(combine, "selection_tool_mode_add", i18n("Add to Selection"));
        menuCommand(combine, "selection_tool_mode_subtract", i18n("Subtract from Selection"));
        menuCommand(combine, "selection_tool_mode_intersect", i18n("Intersect Selection"));
        commandButton("invert_selection", i18n("Invert"));
        commandButton("deselect", i18n("Deselect"));
    } else if (id == QLatin1String("KritaTransform/KisToolMove")) {
        const auto nudge = [&commandButton](const char *id, const QString &text, const QString &description) {
            auto *b = commandButton(id, text);
            b->setAccessibleName(description);
            b->setToolTip(description);
            b->setAutoRepeat(true);
        };
        nudge("movetool-move-left", QStringLiteral("←"), i18n("Nudge left"));
        nudge("movetool-move-up", QStringLiteral("↑"), i18n("Nudge up"));
        nudge("movetool-move-down", QStringLiteral("↓"), i18n("Nudge down"));
        nudge("movetool-move-right", QStringLiteral("→"), i18n("Nudge right"));
    }
    if (transform || crop) {
        connect(button(i18n("Cancel")), &QToolButton::clicked, this, [this] {
            if (auto image = m_manager->image()) image->requestStrokeCancellation();
        });
        auto *apply = button(i18n("Apply"));
        apply->setObjectName(QStringLiteral("tabletApply"));
        connect(apply, &QToolButton::clicked, this, [this] {
            if (auto *canvas = m_manager->canvasBase()) KisToolInvocationAction::confirm(canvas);
        });
    }
    const bool active = m_layout->count() > 0;
    setVisible(active);
    return active;
}
