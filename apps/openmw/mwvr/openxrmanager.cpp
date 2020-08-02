#include "openxrmanager.hpp"
#include "vrenvironment.hpp"
#include "openxrmanagerimpl.hpp"
#include "../mwinput/inputmanagerimp.hpp"

#include <components/debug/debuglog.hpp>

namespace MWVR
{
    OpenXRManager::OpenXRManager()
        : mPrivate(nullptr)
        , mMutex()
    {

    }

    OpenXRManager::~OpenXRManager()
    {

    }

    bool
        OpenXRManager::realized()
    {
        return !!mPrivate;
    }

    bool OpenXRManager::xrSessionRunning()
    {
        if (realized())
            return impl().xrSessionRunning();
        return false;
    }

    bool OpenXRManager::xrSessionCanRender()
    {
        if (realized())
            return impl().xrSessionCanRender();
        return false;
    }

    void OpenXRManager::handleEvents()
    {
        if (realized())
            return impl().handleEvents();
    }

    FrameInfo OpenXRManager::waitFrame()
    {
        return impl().waitFrame();
    }

    void OpenXRManager::beginFrame()
    {
        return impl().beginFrame();
    }

    void OpenXRManager::endFrame(FrameInfo frameInfo, int layerCount, const std::array<CompositionLayerProjectionView, 2>& layerStack)
    {
        return impl().endFrame(frameInfo, layerCount, layerStack);
    }

    void
        OpenXRManager::realize(
            osg::GraphicsContext* gc)
    {
        lock_guard lock(mMutex);
        if (!realized())
        {
            gc->makeCurrent();
            try {
                mPrivate = std::make_shared<OpenXRManagerImpl>();
            }
            catch (std::exception& e)
            {
                Log(Debug::Error) << "Exception thrown by OpenXR: " << e.what();
                osg::ref_ptr<osg::State> state = gc->getState();

            }
        }
    }

    void OpenXRManager::enablePredictions()
    {
        return impl().enablePredictions();
    }

    void OpenXRManager::disablePredictions()
    {
        return impl().disablePredictions();
    }

    void OpenXRManager::xrResourceAcquired()
    {
        return impl().xrResourceAcquired();
    }

    void OpenXRManager::xrResourceReleased()
    {
        return impl().xrResourceReleased();
    }

    std::array<View, 2> OpenXRManager::getPredictedViews(int64_t predictedDisplayTime, ReferenceSpace space)
    {
        return impl().getPredictedViews(predictedDisplayTime, space);
    }

    MWVR::Pose OpenXRManager::getPredictedHeadPose(int64_t predictedDisplayTime, ReferenceSpace space)
    {
        return impl().getPredictedHeadPose(predictedDisplayTime, space);
    }

    long long OpenXRManager::getLastPredictedDisplayTime()
    {
        return impl().getLastPredictedDisplayTime();
    }

    long long OpenXRManager::getLastPredictedDisplayPeriod()
    {
        return impl().getLastPredictedDisplayPeriod();
    }

    std::array<SwapchainConfig, 2> OpenXRManager::getRecommendedSwapchainConfig() const
    {
        return impl().getRecommendedSwapchainConfig();
    }

    bool OpenXRManager::xrExtensionIsEnabled(const char* extensionName) const
    {
        return impl().xrExtensionIsEnabled(extensionName);
    }

    void
        OpenXRManager::CleanupOperation::operator()(
            osg::GraphicsContext* gc)
    {
        // TODO: Use this to make proper cleanup such as cleaning up VRFramebuffers.
    }
}

