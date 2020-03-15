#include "openxrworldview.hpp"
#include "vrenvironment.hpp"
#include "openxrmanager.hpp"
#include "openxrmanagerimpl.hpp"
#include "../mwinput/inputmanagerimp.hpp"
#include "openxrinputmanager.hpp"

#include <components/debug/debuglog.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>

#include <Windows.h>

#include <openxr/openxr.h>

#include "../mwrender/vismask.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/camera.hpp"

#include <osg/Camera>
#include <osgViewer/Renderer>

#include <vector>
#include <array>
#include <iostream>

namespace MWVR
{
    // Some headers like to define these.
#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

    static osg::Matrix
        perspectiveFovMatrix(float near, float far, XrFovf fov)
    {
        const float tanLeft = tanf(fov.angleLeft);
        const float tanRight = tanf(fov.angleRight);
        const float tanDown = tanf(fov.angleDown);
        const float tanUp = tanf(fov.angleUp);

        const float tanWidth = tanRight - tanLeft;
        const float tanHeight = tanUp - tanDown;

        const float offset = near;

        float matrix[16] = {};

        matrix[0] = 2 / tanWidth;
        matrix[4] = 0;
        matrix[8] = (tanRight + tanLeft) / tanWidth;
        matrix[12] = 0;

        matrix[1] = 0;
        matrix[5] = 2 / tanHeight;
        matrix[9] = (tanUp + tanDown) / tanHeight;
        matrix[13] = 0;

        if (far <= near) {
            matrix[2] = 0;
            matrix[6] = 0;
            matrix[10] = -1;
            matrix[14] = -(near + offset);
        }
        else {
            matrix[2] = 0;
            matrix[6] = 0;
            matrix[10] = -(far + offset) / (far - near);
            matrix[14] = -(far * (near + offset)) / (far - near);
        }

        matrix[3] = 0;
        matrix[7] = 0;
        matrix[11] = -1;
        matrix[15] = 0;

        return osg::Matrix(matrix);
    }


    osg::Matrix OpenXRWorldView::projectionMatrix()
    {
        // TODO: Get this from the session predictions instead
        auto hmdViews = mXR->impl().getPredictedViews(mXR->impl().frameState().predictedDisplayTime, TrackedSpace::STAGE);

        float near = Settings::Manager::getFloat("near clip", "Camera");
        float far = Settings::Manager::getFloat("viewing distance", "Camera");
        //return perspectiveFovMatrix()
        if(mName == "LeftEye")
            return perspectiveFovMatrix(near, far, hmdViews[0].fov);
        return perspectiveFovMatrix(near, far, hmdViews[1].fov);
    }

    osg::Matrix OpenXRWorldView::viewMatrix()
    {
        MWVR::Pose pose = predictedPose();
        mXR->playerScale(pose);
        osg::Vec3 position = pose.position * Environment::get().unitsPerMeter();
        osg::Quat orientation = pose.orientation;

        float y = position.y();
        float z = position.z();
        position.y() = z;
        position.z() = -y;

        y = orientation.y();
        z = orientation.z();
        orientation.y() = z;
        orientation.z() = -y;

        osg::Matrix viewMatrix;
        viewMatrix.setTrans(-position);
        viewMatrix.postMultRotate(orientation.conj());

        return viewMatrix;
    }

    OpenXRWorldView::OpenXRWorldView(
        osg::ref_ptr<OpenXRManager> XR, 
        std::string name, 
        osg::ref_ptr<osg::State> state, 
        OpenXRSwapchain::Config config)
        : OpenXRView(XR, name, config, state)
    {
    }

    OpenXRWorldView::~OpenXRWorldView()
    {

    }

    void OpenXRWorldView::InitialDrawCallback::operator()(osg::RenderInfo& renderInfo) const
    {
        osg::GraphicsOperation* graphicsOperation = renderInfo.getCurrentCamera()->getRenderer();
        osgViewer::Renderer* renderer = dynamic_cast<osgViewer::Renderer*>(graphicsOperation);
        if (renderer != nullptr)
        {
            // Disable normal OSG FBO camera setup because it will undo the MSAA FBO configuration.
            renderer->setCameraRequiresSetUp(false);
        }
    }

    void
        OpenXRWorldView::UpdateSlaveCallback::updateSlave(
            osg::View& view,
            osg::View::Slave& slave)
    {
        mView->mTimer.checkpoint("UpdateSlave");
        auto* camera = slave._camera.get();
        auto name = camera->getName();

        auto& poses = mSession->predictedPoses();

        Pose viewPose{};
        Pose stagePose{};

        if (name == "LeftEye")
        {
            mSession->waitFrame();

            // Updating the head pose needs to happen after waitFrame(),
            // But i can't call waitFrame from the input manager since it might
            // not always be active.
            auto* inputManager = Environment::get().getInputManager();
            if (inputManager)
                inputManager->updateHead();

            stagePose = poses.eye[(int)TrackedSpace::STAGE][(int)Side::LEFT_HAND];
            viewPose = poses.eye[(int)TrackedSpace::VIEW][(int)Side::LEFT_HAND];
            mView->setPredictedPose(viewPose);
        }
        else
        {
            stagePose = poses.eye[(int)TrackedSpace::STAGE][(int)Side::RIGHT_HAND];
            viewPose = poses.eye[(int)TrackedSpace::VIEW][(int)Side::RIGHT_HAND];
            mView->setPredictedPose(viewPose);
        }
        if (!mXR->sessionRunning())
            return;

        auto viewMatrix = view.getCamera()->getViewMatrix();
        auto modifiedViewMatrix = viewMatrix * mView->viewMatrix();
        auto projectionMatrix = mView->projectionMatrix();

        camera->setViewMatrix(modifiedViewMatrix);
        camera->setProjectionMatrix(projectionMatrix);
        slave.updateSlaveImplementation(view);
    }
}
