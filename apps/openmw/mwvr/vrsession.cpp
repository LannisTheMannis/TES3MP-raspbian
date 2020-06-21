#include "vrenvironment.hpp"
#include "vrinputmanager.hpp"
#include "openxrmanager.hpp"
#include "openxrswapchain.hpp"
#include "../mwinput/inputmanagerimp.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"

#include <components/debug/debuglog.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>

#include <Windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_platform_defines.h>
#include <openxr/openxr_reflection.h>

#include <osg/Camera>

#include <vector>
#include <array>
#include <iostream>
#include "vrsession.hpp"
#include "vrgui.hpp"
#include <time.h>
#include <thread>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace MWVR
{
VRSession::VRSession()
{
}

VRSession::~VRSession()
{
}

osg::Matrix VRSession::projectionMatrix(FramePhase phase, Side side)
{
    assert(((int)side) < 2);
    auto fov = predictedPoses(VRSession::FramePhase::Update).view[(int)side].fov;
    float near_ = Settings::Manager::getFloat("near clip", "Camera");
    float far_ = Settings::Manager::getFloat("viewing distance", "Camera");
    return fov.perspectiveMatrix(near_, far_);
}

osg::Matrix VRSession::viewMatrix(FramePhase phase, Side side)
{
    MWVR::Pose pose{};
    pose = predictedPoses(phase).view[(int)side].pose;


    if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_NoGame)
    {
        pose = predictedPoses(phase).eye[(int)side];
        osg::Vec3 position = pose.position * Environment::get().unitsPerMeter();
        osg::Quat orientation = pose.orientation;
        osg::Vec3d forward = orientation * osg::Vec3d(0, 1, 0);
        osg::Vec3d up = orientation * osg::Vec3d(0, 0, 1);
        osg::Matrix viewMatrix;
        viewMatrix.makeLookAt(position, position + forward, up);

        return viewMatrix;
    }

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

bool VRSession::isRunning() const {
    auto* xr = Environment::get().getManager();
    return xr->sessionRunning();
}

void VRSession::swapBuffers(osg::GraphicsContext* gc, VRViewer& viewer)
{
    auto* xr = Environment::get().getManager();

    beginPhase(FramePhase::Swap);

    auto* frameMeta = getFrame(FramePhase::Swap).get();

    if (frameMeta->mShouldRender && isRunning())
    {
        auto leftView = viewer.mViews["LeftEye"];
        auto rightView = viewer.mViews["RightEye"];

        viewer.blitEyesToMirrorTexture(gc);
        gc->swapBuffersImplementation();
        leftView->swapBuffers(gc);
        rightView->swapBuffers(gc);

        std::array<CompositionLayerProjectionView, 2> layerStack{};
        layerStack[(int)Side::LEFT_SIDE].swapchain = &leftView->swapchain();
        layerStack[(int)Side::RIGHT_SIDE].swapchain = &rightView->swapchain();
        layerStack[(int)Side::LEFT_SIDE].pose = frameMeta->mPredictedPoses.eye[(int)Side::LEFT_SIDE] / mPlayerScale;
        layerStack[(int)Side::RIGHT_SIDE].pose = frameMeta->mPredictedPoses.eye[(int)Side::RIGHT_SIDE] / mPlayerScale;
        layerStack[(int)Side::LEFT_SIDE].fov = frameMeta->mPredictedPoses.view[(int)Side::LEFT_SIDE].fov;
        layerStack[(int)Side::RIGHT_SIDE].fov = frameMeta->mPredictedPoses.view[(int)Side::RIGHT_SIDE].fov;

        Log(Debug::Debug) << frameMeta->mFrameNo << ": EndFrame " <<std::this_thread::get_id();
        xr->endFrame(frameMeta->mPredictedDisplayTime, 1, layerStack);
    }

    {
        std::unique_lock<std::mutex> lock(mMutex);

        // Some of these values are useless until the prediction time bug is resolved by oculus.
        auto now = std::chrono::steady_clock::now();
        mLastFrameInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(now - mLastRenderedFrameTimestamp);
        mLastRenderedFrameTimestamp = now;
        mLastRenderedFrame = getFrame(FramePhase::Swap)->mFrameNo;

        // Using this to track framerate over the course of gameplay, rather than just seeing the instantaneous
        auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(now - mStart).count();
        static int sBaseFrames = 0;
        if (seconds > 10.f)
        {
            Log(Debug::Verbose) << "Fps: " << (static_cast<double>(mLastRenderedFrame - sBaseFrames) / seconds);
            mStart = now;
            sBaseFrames = mLastRenderedFrame;
        }

        getFrame(FramePhase::Swap) = nullptr;
        mFramesInFlight--;
    }
    mCondition.notify_one();
}

void VRSession::beginPhase(FramePhase phase)
{
    Log(Debug::Debug) << "beginPhase(" << ((int)phase) << ") " << std::this_thread::get_id();

    if (getFrame(phase))
    {
        Log(Debug::Warning) << "advanceFramePhase called with a frame alreay in the target phase";
        return;
    }


    if (phase == FramePhase::Update)
    {
        prepareFrame();
    }
    else
    {
        std::unique_lock<std::mutex> lock(mMutex);
        FramePhase previousPhase = static_cast<FramePhase>((int)phase - 1);
        if (!getFrame(previousPhase))
            throw std::logic_error("beginPhase called without a frame");
        getFrame(phase) = std::move(getFrame(previousPhase));
    }


    // TODO: Invokation should depend on earliest render rather than necessarily phase.
    // Specifically. Without shadows this is fine because nothing is being rendered
    // during cull or earlier.
    // Thought: Add an Shadowmapping phase and invoke it from the shadow code
    // But with shadows rendering occurs during cull and we must do frame sync before those calls.
    // If you want to pay the FPS toll and play with shadows, change FramePhase::Draw to FramePhase::Cull or enjoy your eyes getting torn apart by jitters.
    if (phase == FramePhase::Cull && getFrame(phase)->mShouldRender)
        doFrameSync();
}

void VRSession::doFrameSync()
{
    {
        std::unique_lock<std::mutex> lock(mMutex);
        while (mLastRenderedFrame != mFrames - 1)
        {
            mCondition.wait(lock);
        }
    }

    auto* xr = Environment::get().getManager();
    Log(Debug::Debug) << mFrames << ": WaitFrame " << std::this_thread::get_id();
    xr->waitFrame();
    Log(Debug::Debug) << mFrames << ": BeginFrame " << std::this_thread::get_id();
    xr->beginFrame();
}

std::unique_ptr<VRSession::VRFrameMeta>& VRSession::getFrame(FramePhase phase)
{
    if ((unsigned int)phase >= mFrame.size())
        throw std::logic_error("Invalid frame phase");
    return mFrame[(int)phase];
}

void VRSession::prepareFrame()
{

    std::unique_lock<std::mutex> lock(mMutex);
    mFrames++;
    assert(!mPredrawFrame);

    auto* xr = Environment::get().getManager();
    xr->handleEvents();
    
    //auto frameState = xr->impl().frameState();
//    auto predictedDisplayTime = frameState.predictedDisplayTime;
//    if (predictedDisplayTime == 0)
//    {
//        // First time, need to invent a frame time since openxr won't help us without a call to waitframe.
//        predictedDisplayTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
//    }
//    else
//    {
//        // Predict display time based on real framerate
//        float intervalsf = static_cast<double>(mLastFrameInterval.count()) / static_cast<double>(mLastPredictedDisplayPeriod);
//        int intervals = std::max((int)std::roundf(intervalsf), 1);
//        predictedDisplayTime = mLastPredictedDisplayTime + intervals * (mFrames - mLastRenderedFrame) * mLastPredictedDisplayPeriod;
//    }

//////////////////////// OCULUS BUG
    //////////////////// Oculus will suddenly start monotonically increasing their predicted display time by precisely 1 second
    //////////////////// regardless of real time passed, causing predictions to go crazy due to the time difference.
    //////////////////// Therefore, for the time being, i ignore oculus' predicted display time altogether.
    long long predictedDisplayTime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (mFrames > 1)
    {
        auto framePeriod = xr->getLastPredictedDisplayPeriod();
        float intervalsf = static_cast<double>(mLastFrameInterval.count()) / static_cast<double>(framePeriod);
        int intervals = std::max((int)std::roundf(intervalsf), 1);
        predictedDisplayTime = predictedDisplayTime + intervals * (mFrames - mLastRenderedFrame) * framePeriod;
    }



    PoseSet predictedPoses{};

    xr->enablePredictions();
    predictedPoses.head = xr->getPredictedHeadPose(predictedDisplayTime, TrackedSpace::STAGE) * mPlayerScale;
    auto hmdViews = xr->getPredictedViews(predictedDisplayTime, TrackedSpace::VIEW);
    predictedPoses.view[(int)Side::LEFT_SIDE].pose = hmdViews[(int)Side::LEFT_SIDE].pose * mPlayerScale;
    predictedPoses.view[(int)Side::RIGHT_SIDE].pose = hmdViews[(int)Side::RIGHT_SIDE].pose * mPlayerScale;
    predictedPoses.view[(int)Side::LEFT_SIDE].fov = hmdViews[(int)Side::LEFT_SIDE].fov;
    predictedPoses.view[(int)Side::RIGHT_SIDE].fov = hmdViews[(int)Side::RIGHT_SIDE].fov;
    auto stageViews = xr->getPredictedViews(predictedDisplayTime, TrackedSpace::STAGE);
    predictedPoses.eye[(int)Side::LEFT_SIDE] = stageViews[(int)Side::LEFT_SIDE].pose * mPlayerScale;
    predictedPoses.eye[(int)Side::RIGHT_SIDE] = stageViews[(int)Side::RIGHT_SIDE].pose * mPlayerScale;

    auto* input = Environment::get().getInputManager();
    if (input)
    {
        predictedPoses.hands[(int)Side::LEFT_SIDE] = input->getLimbPose(predictedDisplayTime, TrackedLimb::LEFT_HAND) * mPlayerScale;
        predictedPoses.hands[(int)Side::RIGHT_SIDE] = input->getLimbPose(predictedDisplayTime, TrackedLimb::RIGHT_HAND) * mPlayerScale;
    }
    xr->disablePredictions();

    auto& frame = getFrame(FramePhase::Update);
    frame.reset(new VRFrameMeta);
    frame->mPredictedDisplayTime = predictedDisplayTime;
    frame->mFrameNo = mFrames;
    frame->mPredictedPoses = predictedPoses;
    frame->mShouldRender = isRunning();
    mFramesInFlight++;
}

const PoseSet& VRSession::predictedPoses(FramePhase phase)
{
    auto& frame = getFrame(phase);

    if (phase == FramePhase::Update && !frame)
        beginPhase(FramePhase::Update);

    if (!frame)
        throw std::logic_error("Attempted to get poses from a phase with no current pose");
    return frame->mPredictedPoses;
}

// OSG doesn't provide API to extract euler angles from a quat, but i need it.
// Credits goes to Dennis Bunfield, i just copied his formula https://narkive.com/v0re6547.4
void getEulerAngles(const osg::Quat& quat, float& yaw, float& pitch, float& roll)
{
    // Now do the computation
    osg::Matrixd m2(osg::Matrixd::rotate(quat));
    double* mat = (double*)m2.ptr();
    double angle_x = 0.0;
    double angle_y = 0.0;
    double angle_z = 0.0;
    double D, C, tr_x, tr_y;
    angle_y = D = asin(mat[2]); /* Calculate Y-axis angle */
    C = cos(angle_y);
    if (fabs(C) > 0.005) /* Test for Gimball lock? */
    {
        tr_x = mat[10] / C; /* No, so get X-axis angle */
        tr_y = -mat[6] / C;
        angle_x = atan2(tr_y, tr_x);
        tr_x = mat[0] / C; /* Get Z-axis angle */
        tr_y = -mat[1] / C;
        angle_z = atan2(tr_y, tr_x);
    }
    else /* Gimball lock has occurred */
    {
        angle_x = 0; /* Set X-axis angle to zero
        */
        tr_x = mat[5]; /* And calculate Z-axis angle
        */
        tr_y = mat[4];
        angle_z = atan2(tr_y, tr_x);
    }

    yaw = angle_z;
    pitch = angle_x;
    roll = angle_y;
}

void VRSession::movementAngles(float& yaw, float& pitch)
{
    assert(mPredrawFrame);
    if (!getFrame(FramePhase::Update))
        beginPhase(FramePhase::Update);
    auto frameMeta = getFrame(FramePhase::Update).get();
    
    float headYaw = 0.f;
    float headPitch = 0.f;
    float headsWillRoll = 0.f;

    float handYaw = 0.f;
    float handPitch = 0.f;
    float handRoll = 0.f;

    getEulerAngles(frameMeta->mPredictedPoses.head.orientation, headYaw, headPitch, headsWillRoll);
    getEulerAngles(frameMeta->mPredictedPoses.hands[(int)Side::LEFT_SIDE].orientation, handYaw, handPitch, handRoll);

    //Log(Debug::Verbose) << "lhandViewYaw=" << yaw << ", headYaw=" << headYaw << ", handYaw=" << handYaw << ", diff=" << (handYaw - headYaw);
    //Log(Debug::Verbose) << "lhandViewPitch=" << pitch << ", headPitch=" << headPitch << ", handPitch=" << handPitch << ", diff=" << (handPitch - headPitch);
    yaw = handYaw - headYaw;
    pitch = handPitch - headPitch;
}

}

