#ifndef OPENCS_VIEW_WORLDSPACEWIDGET_H
#define OPENCS_VIEW_WORLDSPACEWIDGET_H

#include <QPoint>
#include <QTimer>

#include <osg/Vec3d>
#include <osg/ref_ptr>

#include <string>
#include <vector>

#include <apps/opencs/view/render/tagbase.hpp>

#include "instancedragmodes.hpp"
#include "objectmarker.hpp"
#include "scenewidget.hpp"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QModelIndex;
class QMouseEvent;
class QObject;
class QWheelEvent;
class QWidget;

namespace CSMDoc
{
    class Document;
}

namespace osg
{
    class Vec3f;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSMWorld
{
    class CellCoordinates;
    class UniversalId;
}

namespace CSVWidget
{
    class SceneToolMode;
    class SceneToolToggle2;
    class SceneToolbar;
    class SceneToolRun;
}

namespace CSVRender
{
    class Cell;
    class EditMode;

    struct WorldspaceHitResult
    {
        bool hit;
        osg::ref_ptr<TagBase> tag;
        unsigned int index0, index1, index2; // indices of mesh vertices
        osg::Vec3d worldPos;
    };

    class WorldspaceWidget : public SceneWidget
    {
        Q_OBJECT

        CSVWidget::SceneToolToggle2* mSceneElements;
        CSVWidget::SceneToolRun* mRun;
        CSMDoc::Document& mDocument;
        unsigned int mInteractionMask;
        CSVWidget::SceneToolMode* mEditMode;
        CSVWidget::SceneToolMode* mCameraMode;
        bool mLocked;
        int mDragMode;
        bool mDragging;
        int mDragX;
        int mDragY;
        bool mSpeedMode;
        double mDragFactor;
        double mDragWheelFactor;
        double mDragShiftFactor;
        QTimer mToolTipDelayTimer;
        QPoint mToolTipPos;
        bool mShowToolTips;
        int mToolTipDelay;
        int mSelectedNavigationMode;

    public:
        enum DropType
        {
            Type_CellsInterior,
            Type_CellsExterior,
            Type_Other,
            Type_DebugProfile
        };

        enum dropRequirments
        {
            canHandle,
            needPaged,
            needUnpaged,
            ignored // either mixed cells, or not cells
        };

        enum InteractionType
        {
            InteractionType_PrimaryEdit,
            InteractionType_PrimarySelect,
            InteractionType_SecondaryEdit,
            InteractionType_SecondarySelect,
            InteractionType_TertiarySelect,
            InteractionType_PrimaryOpen,
            InteractionType_None
        };

        WorldspaceWidget(CSMDoc::Document& document, QWidget* parent = nullptr);
        ~WorldspaceWidget() = default;

        CSVWidget::SceneToolMode* makeNavigationSelector(CSVWidget::SceneToolbar* parent);
        ///< \attention The created tool is not added to the toolbar (via addTool). Doing that
        /// is the responsibility of the calling function.

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolToggle2* makeSceneVisibilitySelector(CSVWidget::SceneToolbar* parent);

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolRun* makeRunTool(CSVWidget::SceneToolbar* parent);

        /// \attention The created tool is not added to the toolbar (via addTool). Doing
        /// that is the responsibility of the calling function.
        CSVWidget::SceneToolMode* makeEditModeSelector(CSVWidget::SceneToolbar* parent);

        void selectDefaultNavigationMode();

        void centerOrbitCameraOnSelection();

        static DropType getDropType(const std::vector<CSMWorld::UniversalId>& data);

        virtual dropRequirments getDropRequirements(DropType type) const;

        virtual void useViewHint(const std::string& hint);
        ///< Default-implementation: ignored.

        /// \return Drop handled?
        virtual bool handleDrop(const std::vector<CSMWorld::UniversalId>& data, DropType type);

        virtual unsigned int getVisibilityMask() const;

        /// \note This function will implicitly add elements that are independent of the
        /// selected edit mode.
        virtual void setInteractionMask(unsigned int mask);

        /// \note This function will only return those elements that are both visible and
        /// marked for interaction.
        unsigned int getInteractionMask() const;

        virtual void setEditLock(bool locked);

        CSMDoc::Document& getDocument();

        /// \param elementMask Elements to be affected by the clear operation
        virtual void clearSelection(int elementMask) = 0;

        /// \param elementMask Elements to be affected by the select operation
        virtual void invertSelection(int elementMask) = 0;

        /// \param elementMask Elements to be affected by the select operation
        virtual void selectAll(int elementMask) = 0;

        // Select everything that references the same ID as at least one of the elements
        // already selected
        //
        /// \param elementMask Elements to be affected by the select operation
        virtual void selectAllWithSameParentId(int elementMask) = 0;

        virtual void selectInsideCube(const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode) = 0;

        virtual void selectWithinDistance(const osg::Vec3d& point, float distance, DragMode dragMode) = 0;

        template <typename Tag>
        std::optional<WorldspaceHitResult> checkTag(
            const osgUtil::LineSegmentIntersector::Intersection& intersection) const;

        std::tuple<osg::Vec3d, osg::Vec3d, osg::Vec3d> getStartEndDirection(int pointX, int pointY) const;

        /// Return the next intersection with scene elements matched by
        /// \a interactionMask based on \a localPos and the camera vector.
        /// If there is no such intersection, instead a point "in front" of \a localPos will be
        /// returned.
        WorldspaceHitResult mousePick(const QPoint& localPos, unsigned int interactionMask) const;

        virtual std::string getCellId(const osg::Vec3f& point) const = 0;

        /// \note Returns the cell if it exists, otherwise a null pointer
        virtual Cell* getCell(const osg::Vec3d& point) const = 0;

        virtual Cell* getCell(const CSMWorld::CellCoordinates& coords) const = 0;

        virtual osg::ref_ptr<TagBase> getSnapTarget(unsigned int elementMask) const = 0;

        virtual std::vector<osg::ref_ptr<TagBase>> getSelection(unsigned int elementMask) const = 0;

        virtual void selectGroup(const std::vector<std::string>&) const = 0;

        virtual void unhideAll() const = 0;

        virtual std::vector<osg::ref_ptr<TagBase>> getEdited(unsigned int elementMask) const = 0;

        virtual void setSubMode(int subMode, unsigned int elementMask) = 0;

        /// Erase all overrides and restore the visual representation to its true state.
        virtual void reset(unsigned int elementMask) = 0;

        EditMode* getEditMode();

        virtual CSVRender::Object* getObjectByReferenceId(const std::string& id) = 0;

        ObjectMarker* getSelectionMarker() { return mSelectionMarker.get(); }
        const ObjectMarker* getSelectionMarker() const { return mSelectionMarker.get(); }

    protected:
        const std::unique_ptr<CSVRender::ObjectMarker> mSelectionMarker;

        /// Visual elements in a scene
        /// @note do not change the enumeration values, they are used in pre-existing button file names!
        enum ButtonId
        {
            Button_Reference = 0x1,
            Button_Pathgrid = 0x2,
            Button_Water = 0x4,
            Button_Terrain = 0x8
        };

        enum CameraMode
        {
            FirstPerson,
            Orbit,
            Free
        };

        virtual void addVisibilitySelectorButtons(CSVWidget::SceneToolToggle2* tool);

        virtual void addEditModeSelectorButtons(CSVWidget::SceneToolMode* tool);

        virtual void updateOverlay();

        void mouseMoveEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

        virtual void handleInteractionPress(const WorldspaceHitResult& hit, InteractionType type);

        void settingChanged(const CSMPrefs::Setting* setting) override;

        void cycleNavigationMode();

    private:
        bool hitBehindMarker(const osg::Vec3d& hitPos) const;

        void handleMarkerHighlight(const int x, const int y);

        void dragEnterEvent(QDragEnterEvent* event) override;

        void dropEvent(QDropEvent* event) override;

        void dragMoveEvent(QDragMoveEvent* event) override;

        virtual std::string getStartupInstruction() = 0;

        void handleInteraction(InteractionType type, bool activate);

    public slots:

        /// \note Drags will be automatically aborted when the aborting is triggered
        /// (either explicitly or implicitly) from within this class. This function only
        /// needs to be called, when the drag abort is triggered externally (e.g. from
        /// an edit mode).
        void abortDrag();

    private slots:

        virtual void referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void referenceableAboutToBeRemoved(const QModelIndex& parent, int start, int end) = 0;

        virtual void referenceableAdded(const QModelIndex& index, int start, int end) = 0;

        virtual void referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void referenceAboutToBeRemoved(const QModelIndex& parent, int start, int end) = 0;

        virtual void referenceAdded(const QModelIndex& index, int start, int end) = 0;

        virtual void pathgridDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) = 0;

        virtual void pathgridAboutToBeRemoved(const QModelIndex& parent, int start, int end) = 0;

        virtual void pathgridAdded(const QModelIndex& parent, int start, int end) = 0;

        virtual void runRequest(const std::string& profile);

        void debugProfileDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void debugProfileAboutToBeRemoved(const QModelIndex& parent, int start, int end);

        void editModeChanged(const std::string& id);

        void showToolTip();

        void primaryOpen(bool activate);

        void primaryEdit(bool activate);

        void secondaryEdit(bool activate);

        void primarySelect(bool activate);

        void secondarySelect(bool activate);

        void tertiarySelect(bool activate);

        void speedMode(bool activate);

        void toggleHiddenInstances();

    protected slots:

        void elementSelectionChanged();

    signals:

        void closeRequest();

        void dataDropped(const std::vector<CSMWorld::UniversalId>& data);

        void requestFocus(const std::string& id);

        friend class MouseState;
    };
}

#endif
