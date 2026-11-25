/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_STUDIO_ANIMATION_H
#define KIS_TOUCH_STUDIO_ANIMATION_H

#include <QFrame>
#include <QPointer>
#include <QVector>

class QAction;
class QLabel;
class QListView;
class QToolButton;
class KisCanvasAnimationState;
class KisTouchStudioFrameModel;
class KisViewManager;

class KisTouchStudioAnimation : public QFrame
{
public:
    explicit KisTouchStudioAnimation(KisViewManager *viewManager, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class FrameEdit { Add, Duplicate, Remove };

    void bindCanvas();
    void refreshFrames();
    void refreshCurrentFrame();
    void seek(int frame, bool finalize = true);
    void editFrame(FrameEdit edit);

    KisViewManager *m_viewManager;
    QPointer<KisCanvasAnimationState> m_state;
    QVector<QMetaObject::Connection> m_canvasConnections;
    KisTouchStudioFrameModel *m_frames;
    QListView *m_timeline;
    QToolButton *m_play;
    QToolButton *m_add;
    QToolButton *m_options;
    QAction *m_duplicate;
    QAction *m_remove;
    QLabel *m_frameNumber;
    QLabel *m_frameRate;
    bool m_scrubbing = false;
};

#endif
