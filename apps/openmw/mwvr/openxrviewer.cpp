#include "openxrviewer.hpp"
#include "openxrmanagerimpl.hpp"
#include "Windows.h"
#include "../mwrender/vismask.hpp"

namespace MWVR
{

    OpenXRViewer::OpenXRViewer(
        osg::ref_ptr<OpenXRManager> XR,
        osg::ref_ptr<osgViewer::Viewer> viewer,
        float metersPerUnit)
        : mXR(XR)
        , mRealizeOperation(new RealizeOperation(XR, this))
        , mViewer(viewer)
        , mMetersPerUnit(metersPerUnit)
        , mConfigured(false)
        , mCompositionLayerProjectionViews(2, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW})
    {
        mViewer->setRealizeOperation(mRealizeOperation);
    }

    OpenXRViewer::~OpenXRViewer(void)
    {
    }

    void 
        OpenXRViewer::traverse(
            osg::NodeVisitor& visitor)
    {
        Log(Debug::Verbose) << "Traversal";
        if (mRealizeOperation->realized())
            osg::Group::traverse(visitor);
    }

    const XrCompositionLayerBaseHeader*
        OpenXRViewer::layer()
    {
        auto stageViews = mXR->impl().getPredictedViews(OpenXRFrameIndexer::instance().mRenderIndex, TrackedSpace::STAGE);
        mCompositionLayerProjectionViews[0].pose = stageViews[0].pose;
        mCompositionLayerProjectionViews[1].pose = stageViews[1].pose;
        mCompositionLayerProjectionViews[0].fov = stageViews[0].fov;
        mCompositionLayerProjectionViews[1].fov = stageViews[1].fov;
        return reinterpret_cast<XrCompositionLayerBaseHeader*>(mLayer.get());
    }

    void OpenXRViewer::traversals()
    {
        Log(Debug::Verbose) << "Pre-Update";
        mXR->handleEvents();
        OpenXRFrameIndexer::instance().advanceUpdateIndex();
        mViewer->updateTraversal();
        Log(Debug::Verbose) << "Post-Update";
        Log(Debug::Verbose) << "Pre-Rendering";
        mViewer->renderingTraversals();
        Log(Debug::Verbose) << "Post-Rendering";
    }

    void OpenXRViewer::realize(osg::GraphicsContext* context)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mConfigured)
            return;

        if (!context->isCurrent())
        if (!context->makeCurrent())
        {
            throw std::logic_error("OpenXRViewer::configure() failed to make graphics context current.");
            return;
        }

        mMainCamera = mViewer->getCamera();
        mMainCamera->setName("Main");
        mMainCamera->setInitialDrawCallback(new OpenXRView::InitialDrawCallback());

        // Use the main camera to render any GUI to the OpenXR GUI quad's swapchain.
        // (When swapping the window buffer we'll blit the mirror texture to it instead.)
        mMainCamera->setCullMask(MWRender::Mask_GUI);

        osg::Vec4 clearColor = mMainCamera->getClearColor();

        if (!mXR->realized())
            mXR->realize(context);

        mViews[OpenXRWorldView::LEFT_VIEW] = new OpenXRWorldView(mXR, "LeftEye", context->getState(), mMetersPerUnit, OpenXRWorldView::LEFT_VIEW);
        mViews[OpenXRWorldView::RIGHT_VIEW] = new OpenXRWorldView(mXR, "RightEye", context->getState(), mMetersPerUnit, OpenXRWorldView::RIGHT_VIEW);

        mLeftCamera = mViews[OpenXRWorldView::LEFT_VIEW]->createCamera(OpenXRWorldView::LEFT_VIEW, clearColor, context);
        mRightCamera = mViews[OpenXRWorldView::RIGHT_VIEW]->createCamera(OpenXRWorldView::RIGHT_VIEW, clearColor, context);
        // Stereo cameras should only draw the scene (AR layers should later add minimap, health, etc.)

        //mLeftCamera->setGraphicsContext(mLeftGW);
        //mRightCamera->setGraphicsContext(mRightGW);

        mLeftCamera->setCullMask(~MWRender::Mask_GUI);
        mRightCamera->setCullMask(~MWRender::Mask_GUI);

        mLeftCamera->setName("LeftEye");
        mRightCamera->setName("RightEye");

        mViewer->addSlave(mLeftCamera, mViews[OpenXRWorldView::LEFT_VIEW]->projectionMatrix(), mViews[OpenXRWorldView::LEFT_VIEW]->viewMatrix(), true);
        mViewer->addSlave(mRightCamera, mViews[OpenXRWorldView::RIGHT_VIEW]->projectionMatrix(), mViews[OpenXRWorldView::RIGHT_VIEW]->viewMatrix(), true);

        mViewer->getSlave(OpenXRWorldView::LEFT_VIEW)._updateSlaveCallback = new OpenXRWorldView::UpdateSlaveCallback(mXR, mViews[OpenXRWorldView::LEFT_VIEW], context);
        mViewer->getSlave(OpenXRWorldView::RIGHT_VIEW)._updateSlaveCallback = new OpenXRWorldView::UpdateSlaveCallback(mXR, mViews[OpenXRWorldView::RIGHT_VIEW], context);

        mViewer->setLightingMode(osg::View::SKY_LIGHT);
        mViewer->setReleaseContextAtEndOfFrameHint(false);

        // Rendering main camera is a waste of time with VR enabled
        //camera->setGraphicsContext(nullptr);
        mXRInput.reset(new OpenXRInputManager(mXR));


        mLayer.reset(new XrCompositionLayerProjection);
        mLayer->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        mLayer->space = mXR->impl().mReferenceSpaceStage;
        mLayer->viewCount = 2;
        mLayer->views = mCompositionLayerProjectionViews.data();
        mCompositionLayerProjectionViews[OpenXRWorldView::LEFT_VIEW].subImage = mViews[OpenXRWorldView::LEFT_VIEW]->swapchain().subImage();
        mCompositionLayerProjectionViews[OpenXRWorldView::RIGHT_VIEW].subImage = mViews[OpenXRWorldView::RIGHT_VIEW]->swapchain().subImage();

        mXR->impl().mLayerStack.setLayer(OpenXRLayerStack::WORLD_VIEW_LAYER, this);

        OpenXRSwapchain::Config config;
        config.requestedFormats = {
                GL_RGBA8,
                GL_RGBA8_SNORM,
        };
        config.width = mMainCamera->getViewport()->width();
        config.height = mMainCamera->getViewport()->height();
        config.samples = 1;

        // Mirror texture doesn't have to be an OpenXR swapchain.
        // It's just convenient.
        mMirrorTextureSwapchain.reset(new OpenXRSwapchain(mXR, context->getState(), config));

        mXRMenu.reset(new OpenXRMenu(mXR, context->getState(), "MainMenu", config.width, config.height, osg::Vec2(1.f, 1.f)));
        mMenuCamera = mXRMenu->createCamera(2, clearColor, context);
        mMenuCamera->setCullMask(MWRender::Mask_GUI);
        mMenuCamera->setName("MenuCamera");
        mViewer->addSlave(mMenuCamera, true);
        //mMenuCamera->setPreDrawCallback(new OpenXRView::PredrawCallback(mMenuCamera, mXRMenu.get()));
        //mMenuCamera->setFinalDrawCallback(new OpenXRView::PostdrawCallback(mMenuCamera, mXRMenu.get()));
        mXR->impl().mLayerStack.setLayer(OpenXRLayerStack::MENU_VIEW_LAYER, mXRMenu.get());

        mMainCamera->getGraphicsContext()->setSwapCallback(new OpenXRViewer::SwapBuffersCallback(this));
        mMainCamera->setGraphicsContext(nullptr);
        mConfigured = true;
    }

    void OpenXRViewer::blitEyesToMirrorTexture(osg::GraphicsContext* gc) const
    {
        mMirrorTextureSwapchain->beginFrame(gc);
        mViews[OpenXRWorldView::LEFT_VIEW]->swapchain().renderBuffer()->blit(gc, 0, 0, mMirrorTextureSwapchain->width() / 2, mMirrorTextureSwapchain->height());
        mViews[OpenXRWorldView::RIGHT_VIEW]->swapchain().renderBuffer()->blit(gc, mMirrorTextureSwapchain->width() / 2, 0, mMirrorTextureSwapchain->width(), mMirrorTextureSwapchain->height());

        //mXRMenu->swapchain().current()->blit(gc, 0, 0, mMirrorTextureSwapchain->width() / 2, mMirrorTextureSwapchain->height());
    }

    void
        OpenXRViewer::SwapBuffersCallback::swapBuffersImplementation(
            osg::GraphicsContext* gc)
    {
        mViewer->swapBuffers(gc);
    }

    void OpenXRViewer::swapBuffers(osg::GraphicsContext* gc)
    {
        Timer timer("swapBuffers");

        Log(Debug::Verbose) << "SwapBuffers()";
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mConfigured)
            return;

        auto* state = gc->getState();
        auto* gl = osg::GLExtensions::Get(state->getContextID(), false);


        mXR->beginFrame();
        //blitEyesToMirrorTexture(gc);
        mViews[OpenXRWorldView::LEFT_VIEW]->swapchain().endFrame(gc);
        mViews[OpenXRWorldView::RIGHT_VIEW]->swapchain().endFrame(gc);
        mXRMenu->swapchain().endFrame(gc);
        mXR->endFrame();
        mXR->waitFrame();
        //gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER_EXT, 0);
        
        //mMirrorTextureSwapchain->renderBuffer()->blit(gc, 0, 0, mMirrorTextureSwapchain->width(), mMirrorTextureSwapchain->height());

        //mMirrorTextureSwapchain->endFrame(gc);
        gc->swapBuffersImplementation();
    }

    void
        OpenXRViewer::RealizeOperation::operator()(
            osg::GraphicsContext* gc)
    {
        OpenXRManager::RealizeOperation::operator()(gc);
        mViewer->realize(gc);
    }

    bool
        OpenXRViewer::RealizeOperation::realized()
    {
        return mViewer->realized();
    }
}
