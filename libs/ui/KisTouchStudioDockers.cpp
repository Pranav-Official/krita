/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchStudioDockers.h"

#include <algorithm>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScroller>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <klocalizedstring.h>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "widgets/kis_tool_options_popup.h"

KisTouchStudioDockers::KisTouchStudioDockers(KisViewManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_window(manager->mainWindow())
    , m_stack(new QStackedWidget(this))
    , m_search(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_title(new QLabel(this))
    , m_back(new QToolButton(this))
    , m_scroll(new QScrollArea(this))
{
    setObjectName(QStringLiteral("tabletStudios"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);
    auto *heading = new QHBoxLayout;
    m_back->setArrowType(Qt::LeftArrow);
    m_back->setFixedSize(44, 44);
    m_back->setAccessibleName(i18n("Back to studios"));
    m_back->setToolTip(m_back->accessibleName());
    m_back->setObjectName(QStringLiteral("studioBack"));
    heading->addWidget(m_back);
    m_title->setObjectName(QStringLiteral("studioTitle"));
    m_title->setWordWrap(true);
    heading->addWidget(m_title, 1);
    outer->addLayout(heading);
    outer->addWidget(m_stack);
    auto *library = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(library);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_search->setPlaceholderText(i18n("Find a studio"));
    m_search->setAccessibleName(i18n("Search studios"));
    m_search->setClearButtonEnabled(true);
    m_search->setObjectName(QStringLiteral("studioSearch"));
    m_search->setMinimumHeight(42);
    m_list->setObjectName(QStringLiteral("studioLibrary"));
    m_list->setAccessibleName(i18n("Studios"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(m_list->viewport(), QScroller::TouchGesture);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    auto *empty = new QLabel(i18n("No matching studios"), library);
    empty->setAlignment(Qt::AlignCenter);
    empty->setObjectName(QStringLiteral("studioEmpty"));
    layout->addWidget(empty, 1);
    empty->hide();
    m_stack->addWidget(library);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setObjectName(QStringLiteral("studioInspector"));
    m_stack->addWidget(m_scroll);

    // Limit styling to the navigator. Plugin widgets keep their own color
    // management, palettes, custom delegates, and geometry requirements.
    setStyleSheet(QStringLiteral(R"(
        QLabel#studioTitle { color: #efeff1; font-size: 20px; font-weight: 600; }
        QLabel#studioEmpty { color: #c4c5cb; }
        QToolButton#studioBack { color: #efeff1; background: #35363a; border: 1px solid transparent; border-radius: 6px; }
        QLineEdit#studioSearch { color: #efeff1; background: #1e1f22; border: 1px solid #47484d; border-radius: 7px; padding: 0 10px; }
        QLineEdit#studioSearch:focus, QToolButton#studioBack:focus { border-color: #88baff; }
        QListWidget#studioLibrary { background: transparent; color: #efeff1; }
        QListWidget#studioLibrary::item { border: none; border-bottom: 1px solid #37383c; padding-left: 8px; }
        QListWidget#studioLibrary::item:hover { background: #343538; }
        QListWidget#studioLibrary::item:selected { background: #395f88; color: white; }
    )"));

    connect(m_back, &QToolButton::clicked, this, &KisTouchStudioDockers::showLibrary);
    connect(m_search, &QLineEdit::textChanged, this, [this, empty](const QString &query) {
        bool found = false;
        for (int row = 0; row < m_list->count(); ++row) {
            auto *item = m_list->item(row);
            item->setHidden(!item->text().contains(query.trimmed(), Qt::CaseInsensitive));
            found |= !item->isHidden();
        }
        empty->setVisible(!found && !query.isEmpty());
        m_list->setVisible(found || query.isEmpty());
    });
    const auto select = [this](QListWidgetItem *item) {
        if (m_window && item) {
            openDocker(m_window->dockWidget(item->data(Qt::UserRole).toString()));
        }
    };
    connect(m_list, &QListWidget::itemClicked, this, select);
    connect(m_list, &QListWidget::itemActivated, this, select);
    showLibrary();
}

KisTouchStudioDockers::~KisTouchStudioDockers()
{
    dismiss();
}

void KisTouchStudioDockers::showLibrary()
{
    releaseDocker();
    m_lastStudio.clear();
    m_stack->setCurrentIndex(0);
    m_back->hide();
    m_title->setText(i18n("Studios"));
    m_search->clear();
    m_list->clear();
    if (!m_window) return;

    auto docks = m_window->dockWidgets();
    std::sort(docks.begin(), docks.end(), [](const QDockWidget *a, const QDockWidget *b) {
        return QString::localeAwareCompare(a->windowTitle(), b->windowTitle()) < 0;
    });
    for (auto *dock : docks) {
        auto *item = new QListWidgetItem(dock->windowIcon(), dock->windowTitle(), m_list);
        item->setData(Qt::UserRole, dock->objectName());
        item->setSizeHint(QSize(0, 48));
    }
}

void KisTouchStudioDockers::showToolOptions(bool options)
{
    m_options = options;
    if (!options || !m_window) {
        if (m_window && !m_lastStudio.isEmpty()) {
            if (auto *dock = m_window->dockWidget(m_lastStudio)) {
                openDocker(dock);
                return;
            }
        }
        showLibrary();
        return;
    }

    if (auto *dock = m_window->dockWidget(QStringLiteral("sharedtooldocker"))) {
        openDocker(dock);
        m_back->hide();
        return;
    }

    // The alternative desktop placement keeps the same live tool options in
    // a popup frame. Borrow its contents and return them to that exact frame.
    if (!m_optionsPopup) {
        releaseDocker();
        auto *popup = m_window->findChild<KisToolOptionsPopup *>(QStringLiteral("KisToolOptionsPopup"));
        if (popup) {
            m_optionsPopup = popup;
            m_popupParent = popup->parentWidget();
            m_popupHidden = popup->isHidden();
            m_scroll->setWidget(popup);
            popup->show();
        }
    }
    m_title->setText(i18n("Tool Options"));
    m_back->hide();
    m_stack->setCurrentWidget(m_scroll);
}

void KisTouchStudioDockers::openDocker(QDockWidget *dock)
{
    if (!m_window || !dock || dock == m_dock) return;
    releaseDocker();
    if (!m_options) m_lastStudio = dock->objectName();
    m_dock = dock;
    m_area = m_window->dockWidgetArea(dock);
    m_layoutBeforeMount = m_window->saveState();
    m_features = dock->features();
    m_titleBar = dock->titleBarWidget();
    m_titleHidden = !m_titleBar || m_titleBar->isHidden();

    m_window->removeDockWidget(dock);
    dock->setParent(m_scroll->viewport(), Qt::Widget);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    if (m_titleBar) m_titleBar->hide();
    m_scroll->setWidget(dock);
    m_title->setText(dock->windowTitle());
    m_back->setVisible(!m_options);
    m_stack->setCurrentWidget(m_scroll);
    dock->show();
}

void KisTouchStudioDockers::releaseDocker()
{
    if (m_optionsPopup) {
        m_scroll->takeWidget();
        m_optionsPopup->setParent(m_popupParent);
        if (m_popupParent && m_popupParent->layout()) {
            m_popupParent->layout()->addWidget(m_optionsPopup);
            m_optionsPopup->setVisible(!m_popupHidden);
        }
        m_optionsPopup.clear();
        m_popupParent.clear();
    }
    if (!m_dock || !m_window) return;

    // The desktop and tablet use the very same docker, which matters for
    // plugins that follow visibility or register actions during construction.
    // Restore tab groups, floating geometry, and hidden state after browsing.
    auto *dock = m_dock.data();
    m_dock.clear();
    dock->hide();
    m_scroll->takeWidget();
    dock->setParent(m_window);
    dock->setFeatures(m_features);
    m_window->addDockWidget(m_area == Qt::NoDockWidgetArea ? Qt::RightDockWidgetArea : m_area, dock);
    m_window->restoreState(m_layoutBeforeMount);
    if (m_titleBar) m_titleBar->setVisible(!m_titleHidden);
    m_titleBar.clear();
    m_layoutBeforeMount.clear();
}

void KisTouchStudioDockers::dismiss()
{
    releaseDocker();
}
