#define STB_IMAGE_IMPLEMENTATION
#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Profiling.hpp>
#include <iostream>
#include <tracy/Tracy.hpp>

App::App()
  : resolution{1280, 720}
  , useVsync{true}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = N_FRAMES_IN_FLIGHT,
    });
  }

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();
  
  // prepare entities for rendering texture image
  etna::create_program("texture", {INFLIGHT_FRAMES_SHADERS_ROOT "texture.comp.spv"});
  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createComputePipeline("texture", {});

  textureImage = etna::get_context().createImage(etna::Image::CreateInfo {
    .extent     = vk::Extent3D{resolution.x, resolution.y, 1},
    .name       = "texture_image", 
    .format     = vk::Format::eR8G8B8A8Snorm,
    .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc 
  });
  
  sampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "sampler"});
  
  // prepare primitives for drawing main frame
  etna::create_program("frame", {INFLIGHT_FRAMES_SHADERS_ROOT "toy.frag.spv",
	INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});

  framePipeline = pipelineManager.createGraphicsPipeline("frame", 
	etna::GraphicsPipeline::CreateInfo{
		.fragmentShaderOutput = {
			.colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb},
			.depthAttachmentFormat  = vk::Format::eD32Sfloat
		}});
 
  frameSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "sampler2",
  });
  
  int width, height, channels;
  const auto file = stbi_load(
    INFLIGHT_FRAMES_SHADERS_ROOT "../../../../resources/textures/texture1.bmp",
    &width,
    &height,
    &channels,
    STBI_rgb_alpha);

  image = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<unsigned int>(width), static_cast<unsigned int>(height), 1},
    .name = "texture",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst
      | vk::ImageUsageFlagBits::eSampled
  });
  
  oneShotManager = etna::get_context().createOneShotCmdMgr();

  auto blockingInfo =
    etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = static_cast<std::uint32_t>(width * height),
    };
  etna::BlockingTransferHelper(blockingInfo)
    .uploadImage(
      *oneShotManager,
      image,
      0,
      0,
      std::span(reinterpret_cast<const std::byte*>(file), width * height * 4));
  
  for (auto &constant :constants) {
    constant = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "constants",
    });
    constant.map();
  }
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    drawFrame();
    currentFrame = (currentFrame + 1) % N_FRAMES_IN_FLIGHT;
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}


void App::drawFrame()
{
  {
    glm::vec2 mousePosition = osWindow.get()->mouse.freePos;

    uniformParams.resolution = {1280, 720};
    uniformParams.mouse = {mousePosition.x, mousePosition.y};
    uniformParams.time = static_cast<float>(windowing.getTime());

    std::memcpy(constants[currentFrame].data(), &uniformParams, sizeof(uniformParams));
  }
  ZoneScopedN("drawFrame");
  FrameMark;
  {
    ZoneScopedN("FakeCpuLoad");
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      // First of all, we need to "initialize" th "backbuffer", aka the current swapchain
      // image, into a state that is appropriate for us working with it. The initial state
      // is considered to be "undefined" (aka "I contain trash memory"), by the way.
      // "Transfer" in vulkanese means "copy or blit".
      // Note that Etna sometimes calls this for you to make life simpler, read Etna's code!
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // We are going to use the texture at the transfer stage...
        vk::PipelineStageFlagBits2::eTransfer,
        // ...to transfer-write stuff into it...
        vk::AccessFlagBits2::eTransferWrite,
        // ...and want it to have the appropriate layout.
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageAspectFlagBits::eColor);
      // The set_state doesn't actually record any commands, they are deferred to
      // the moment you call flush_barriers.
      // As with set_state, Etna sometimes flushes on it's own.
      // Usually, flushes should be placed before "action", i.e. compute dispatches
      // and blit/copy operations.
      etna::flush_barriers(currentCmdBuf);

      renderTexture(currentCmdBuf);
{
      ETNA_PROFILE_GPU(currentCmdBuf, "renderFrame"); 
      etna::set_state(
        currentCmdBuf,
        textureImage.get(),
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferRead,
        vk::ImageLayout::eTransferSrcOptimal,
        vk::ImageAspectFlagBits::eColor);
 
      auto localShadertoyInfo = etna::get_shader_program("frame");

      auto set = etna::create_descriptor_set(
      	localShadertoyInfo.getDescriptorLayoutId(0),
      	currentCmdBuf,
      	{etna::Binding{0, textureImage.genBinding(sampler.get(), vk::ImageLayout::eGeneral)},
        etna::Binding{1, constants[currentFrame].genBinding()}}
	).getVkSet();

        // Texture from image
	// {etna::Binding{0, image.genBinding(frameSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}}
      etna::flush_barriers(currentCmdBuf);

      {
 
        etna::RenderTargetState renderTargets{
          currentCmdBuf,
          {{0, 0}, {resolution.x, resolution.y}},
          {{.image = backbuffer, .view = backbufferView}},
          {}};
        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, framePipeline.getVkPipeline());

        currentCmdBuf.bindDescriptorSets(
      	  vk::PipelineBindPoint::eGraphics,
      	  framePipeline.getVkPipelineLayout(),
      	  0,
          1,
      	  &set,
      	  0,
          nullptr);

        currentCmdBuf.draw(3, 1, 0, 0);
        etna::flush_barriers(currentCmdBuf);
}
      }
      // At the end of "rendering", we are required to change how the pixels of the
      // swpchain image are laid out in memory to something that is appropriate
      // for presenting to the window (while preserving the content of the pixels!).
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        // This looks weird, but is correct. Ask about it later.
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      // And of course flush the layout transition.
      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be executed by the GPU.
    // Note that the GPU won't start executing our commands before the semaphore is
    // signalled, which will happen when the OS says that the next swapchain image is ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells the OS
    // that it is done executing the command buffer via the renderingDone semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // After a window us un-minimized, we need to restore the swapchain to continue rendering.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}

void App::renderTexture(vk::CommandBuffer &currentCmdBuf) {
  ETNA_PROFILE_GPU(currentCmdBuf, "renderTexture");
  auto localShadertoyInfo = etna::get_shader_program("texture");

  auto set = etna::create_descriptor_set(
    localShadertoyInfo.getDescriptorLayoutId(0),
    currentCmdBuf,
    {etna::Binding{0, textureImage.genBinding(sampler.get(), vk::ImageLayout::eGeneral)}}).getVkSet(); 

  currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getVkPipeline());

  currentCmdBuf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute,
    pipeline.getVkPipelineLayout(),
    0,
    1,
    &set,
    0, nullptr);
  
  etna::flush_barriers(currentCmdBuf);
  currentCmdBuf.dispatch(resolution.x, resolution.y, 1);
}

