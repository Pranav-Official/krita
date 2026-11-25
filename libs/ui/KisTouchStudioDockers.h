/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_STUDIO_DOCKERS_H
#define KIS_TOUCH_STUDIO_DOCKERS_H

#include <QDockWidget>
#include <QPointer>
#include <QWidget>

class KisMainWindow;
class KisViewManager;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QToolButton;

// Presents the window's existing docker instances, including plugin-provided
// panels. Their canvas subscriptions and document-specific state stay live.
class KisTouchStudioDockers final : public QWidget
{
public:
    KisTouchStudioDockers(KisViewManager *manager, QWidget *parent);
    ~KisTouchStudioDockers() override;

    void showToolOptions(bool options);
    void dismiss();

private:
    void showLibrary();
    void openDocker(QDockWidget *dock);
    void releaseDocker();

    QPointer<KisMainWindow> m_window;
    QPointer<QDockWidget> m_dock;
    QPointer<QWidget> m_titleBar;
    QDockWidget::DockWidgetFeatures m_features;
    Qt::DockWidgetArea m_area = Qt::NoDockWidgetArea;
    QByteArray m_layoutBeforeMount;
    bool m_titleHidden = false;
    bool m_options = false;
    QString m_lastStudio;
    QPointer<QWidget> m_popupParent;
    QPointer<QWidget> m_optionsPopup;
    bool m_popupHidden = false;

    QStackedWidget *m_stack;
    QLineEdit *m_search;
    QListWidget *m_list;
    QLabel *m_title;
    QToolButton *m_back;
    QScrollArea *m_scroll;
};

#endif
