#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include "wsi/OsWindowingManager.hpp"
#include <etna/GraphicsPipeline.hpp>
#include <stb_image.h>
#include "shaders/UniformParams.h"

#define N_FRAMES_IN_FLIGHT 2

class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();
  void renderTexture(vk::CommandBuffer &cmdBuf);
  //void renderFrame(vk::CommandBuffer &cmdBuf);

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  etna::Buffer constants[N_FRAMES_IN_FLIGHT];

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
  etna::ComputePipeline pipeline;
  etna::Image   textureImage;
  etna::Sampler sampler;

  etna::Sampler frameSampler;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotManager;
  etna::GraphicsPipeline framePipeline;
  etna::Image   image;

  uint32_t currentFrame = 0;
  UniformParams uniformParams{
    .resolution = {},
    .mouse = {},
    .time = {},
  };
};
