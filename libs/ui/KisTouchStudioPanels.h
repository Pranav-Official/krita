/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_STUDIO_PANELS_H
#define KIS_TOUCH_STUDIO_PANELS_H

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <kis_types.h>

class KisNodeModel;
class KisPresetChooser;
class KisViewManager;
class KisVisualColorSelector;
class KoColor;
class KoID;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTreeView;

class KisTouchStudioPanels : public QWidget
{
public:
    explicit KisTouchStudioPanels(KisViewManager *viewManager, QWidget *parent = nullptr);

    void showPage(const QString &id);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void syncDocument();
    void syncColor(const KoColor &color);
    void syncLayers();
    void updateLayerButtons();
    void updateLayerOpacity();
    void toggleLayerProperty(const KoID &property);

    QPointer<KisViewManager> m_viewManager;
    QMetaObject::Connection m_imageClosing;
    QMetaObject::Connection m_colorSpaceChanged;
    QMetaObject::Connection m_displayChanged;
    QMetaObject::Connection m_layerOpacityChanged;
    KisNodeWSP m_opacityNode;
    QStackedWidget *m_pages {nullptr};
    KisPresetChooser *m_presets {nullptr};
    KisVisualColorSelector *m_colorSelector {nullptr};
    QLabel *m_colorSample {nullptr};
    QLabel *m_colorValue {nullptr};
    KisNodeModel *m_layerModel {nullptr};
    QTreeView *m_layerView {nullptr};
    QSlider *m_opacitySlider {nullptr};
    QLabel *m_opacityValue {nullptr};
    QPushButton *m_layerMenuButton {nullptr};
    QPushButton *m_visibilityButton {nullptr};
    QPushButton *m_lockButton {nullptr};
    QPushButton *m_removeButton {nullptr};
};

#endif
