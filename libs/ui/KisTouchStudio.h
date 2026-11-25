/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_STUDIO_H
#define KIS_TOUCH_STUDIO_H

#include <QObject>
#include <QPointer>

#include <memory>

class QAction;
class KisViewManager;
class QWidget;

class KisTouchStudio : public QObject
{
public:
    KisTouchStudio(KisViewManager *manager, QWidget *host, QAction *exitAction);
    ~KisTouchStudio() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void layoutOverlays();
    void setPanel(const QString &id);
    void showCommands(const QString &page);
    void collectCommands();
    void populateCommands();
    void updateTools();
    void updateToolIcons(bool erasing);
    void bindCanvasResources();
    void updateBrushControls();

    struct Private;
    const std::unique_ptr<Private> d;
};

#endif
