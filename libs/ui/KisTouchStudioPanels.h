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
class KisCompositeOpComboBox;
class KisTouchStudioDockers;
class KisViewManager;
class KisVisualColorSelector;
class KoColor;
class KoColorSpace;
class KoID;
class KoToolBase;
class QLabel;
class QComboBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTreeView;

class KisTouchStudioPanels : public QWidget
{
    Q_OBJECT

public:
    explicit KisTouchStudioPanels(KisViewManager *viewManager, QWidget *parent = nullptr);
    ~KisTouchStudioPanels() override;

    void showPage(const QString &id);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QWidget *createBrushSettings(QWidget *parent);
    void updateBrushSmoothingTool();
    void syncDocument();
    void syncColor(const KoColor &color);
    void setColorSpace(const KoColorSpace *space);
    void updateColorHistory();
    void syncLayers();
    void updateLayerButtons();
    void updateLayerOpacity();
    void toggleLayerProperty(const KoID &property);

private Q_SLOTS:
    void syncBrushSmoothing();

private:
    QPointer<KisViewManager> m_viewManager;
    QMetaObject::Connection m_imageClosing;
    QMetaObject::Connection m_colorSpaceChanged;
    QMetaObject::Connection m_displayChanged;
    QMetaObject::Connection m_layerOpacityChanged;
    QMetaObject::Connection m_brushSmoothingChanged;
    KisNodeWSP m_opacityNode;
    QStackedWidget *m_pages {nullptr};
    KisPresetChooser *m_presets {nullptr};
    QListView *m_brushTags {nullptr};
    QComboBox *m_compactTags {nullptr};
    QComboBox *m_brushSmoothing {nullptr};
    QPointer<KoToolBase> m_brushSmoothingTool;
    KisTouchStudioDockers *m_dockers {nullptr};
    bool m_showToolOptions {false};
    KisVisualColorSelector *m_colorSelector {nullptr};
    QLabel *m_colorSample {nullptr};
    QLineEdit *m_colorValue {nullptr};
    QWidget *m_colorRgbFields {nullptr};
    QLineEdit *m_colorChannels[3] {};
    QWidget *m_colorHistory {nullptr};
    QWidget *m_colorSwatches {nullptr};
    QWidget *m_graySwatches {nullptr};
    bool m_colorIsGray {false};
    QPushButton *m_historyButtons[6] {};
    KisNodeModel *m_layerModel {nullptr};
    QTreeView *m_layerView {nullptr};
    QSlider *m_opacitySlider {nullptr};
    QLabel *m_opacityValue {nullptr};
    KisCompositeOpComboBox *m_blendMode {nullptr};
    QPushButton *m_layerMenuButton {nullptr};
    QPushButton *m_visibilityButton {nullptr};
    QPushButton *m_lockButton {nullptr};
    QPushButton *m_removeButton {nullptr};
};

#endif
