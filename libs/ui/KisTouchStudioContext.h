/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KIS_TOUCH_STUDIO_CONTEXT_H
#define KIS_TOUCH_STUDIO_CONTEXT_H

#include <QWidget>

class KisViewManager;
class QHBoxLayout;

class KisTouchStudioContext final : public QWidget
{
public:
    KisTouchStudioContext(KisViewManager *manager, QWidget *parent);
    bool updateForTool(const QString &toolId);

private:
    KisViewManager *m_manager;
    QHBoxLayout *m_layout;
    QString m_toolId;
};
#endif
