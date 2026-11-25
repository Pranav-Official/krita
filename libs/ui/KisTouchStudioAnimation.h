/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_STUDIO_ANIMATION_H
#define KIS_TOUCH_STUDIO_ANIMATION_H

#include <QFrame>
#include <QPointer>
#include <QVector>

class QAction;
class QListView;
class QMenu;
class QResizeEvent;
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
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class FrameEdit { Add, Duplicate, Remove };

    void bindCanvas();
    void refreshFrames();
    void refreshCurrentFrame();
    void seek(int frame, bool finalize = true);
    void editFrame(FrameEdit edit);
    void populateTracks();
    void editPlaybackSettings();
    void toggleOnionSkin();
    void updateTrackLabel();

    KisViewManager *m_viewManager;
    QPointer<KisCanvasAnimationState> m_state;
    QVector<QMetaObject::Connection> m_canvasConnections;
    KisTouchStudioFrameModel *m_frames;
    QListView *m_timeline;
    QToolButton *m_previous;
    QToolButton *m_play;
    QToolButton *m_next;
    QToolButton *m_track;
    QToolButton *m_onion;
    QToolButton *m_add;
    QToolButton *m_options;
    QMenu *m_tracks;
    QAction *m_duplicate;
    QAction *m_remove;
    QAction *m_playbackSettings;
    bool m_scrubbing = false;
};

#endif
