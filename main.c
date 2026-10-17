/*
 * ps5-vk-triangle: a spinning triangle on a pulsing background, drawn with
 * Vulkan 1.3 through RADV linked into the program.
 *
 * There is no Vulkan loader on the console: every function comes from the
 * driver's vk_icdGetInstanceProcAddr. The screen is VK_KHR_display (VideoOut):
 * one display, one plane, FIFO presentation.
 *
 * Usage: ps5-vk-triangle [seconds]   (default 30; 0 runs until killed)
 *
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L /* clock_gettime */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "triangle_frag.h"
#include "triangle_vert.h"

#define FRAMES_IN_FLIGHT 2
#define MAX_IMAGES       8

#ifdef USE_VULKAN_LOADER
/* PC only: go through the system loader (and so its validation layers). */
#define get_instance_proc_addr vkGetInstanceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *);
#else
/* The driver's entry point, the only symbol the RADV archive exports. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *);
#define get_instance_proc_addr vk_icdGetInstanceProcAddr
#endif

/* ------------------------------------------------------------ functions */

#define INSTANCE_FUNCS(X)                                                                          \
   X(vkDestroyInstance)                                                                            \
   X(vkEnumeratePhysicalDevices)                                                                   \
   X(vkGetPhysicalDeviceProperties)                                                                \
   X(vkGetPhysicalDeviceQueueFamilyProperties)                                                     \
   X(vkCreateDevice)                                                                               \
   X(vkGetDeviceProcAddr)                                                                          \
   X(vkGetPhysicalDeviceDisplayPropertiesKHR)                                                      \
   X(vkGetPhysicalDeviceDisplayPlanePropertiesKHR)                                                 \
   X(vkGetDisplayModePropertiesKHR)                                                                \
   X(vkGetDisplayPlaneCapabilitiesKHR)                                                             \
   X(vkCreateDisplayPlaneSurfaceKHR)                                                               \
   X(vkDestroySurfaceKHR)                                                                          \
   X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                         \
   X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                    \
   X(vkGetPhysicalDeviceSurfaceFormatsKHR)

#define DEVICE_FUNCS(X)                                                                            \
   X(vkDestroyDevice)                                                                              \
   X(vkGetDeviceQueue)                                                                             \
   X(vkDeviceWaitIdle)                                                                             \
   X(vkCreateSwapchainKHR)                                                                         \
   X(vkDestroySwapchainKHR)                                                                        \
   X(vkGetSwapchainImagesKHR)                                                                      \
   X(vkAcquireNextImageKHR)                                                                        \
   X(vkQueuePresentKHR)                                                                            \
   X(vkCreateImageView)                                                                            \
   X(vkDestroyImageView)                                                                           \
   X(vkCreateShaderModule)                                                                         \
   X(vkDestroyShaderModule)                                                                        \
   X(vkCreatePipelineLayout)                                                                       \
   X(vkDestroyPipelineLayout)                                                                      \
   X(vkCreateGraphicsPipelines)                                                                    \
   X(vkDestroyPipeline)                                                                            \
   X(vkCreateCommandPool)                                                                          \
   X(vkDestroyCommandPool)                                                                         \
   X(vkAllocateCommandBuffers)                                                                     \
   X(vkBeginCommandBuffer)                                                                         \
   X(vkEndCommandBuffer)                                                                           \
   X(vkResetCommandBuffer)                                                                         \
   X(vkCmdPipelineBarrier2)                                                                        \
   X(vkCmdBeginRendering)                                                                          \
   X(vkCmdEndRendering)                                                                            \
   X(vkCmdBindPipeline)                                                                            \
   X(vkCmdSetViewport)                                                                             \
   X(vkCmdSetScissor)                                                                              \
   X(vkCmdPushConstants)                                                                           \
   X(vkCmdDraw)                                                                                    \
   X(vkQueueSubmit2)                                                                               \
   X(vkCreateFence)                                                                                \
   X(vkDestroyFence)                                                                               \
   X(vkWaitForFences)                                                                              \
   X(vkResetFences)                                                                                \
   X(vkCreateSemaphore)                                                                            \
   X(vkDestroySemaphore)

#define DECLARE(name) static PFN_##name name;
INSTANCE_FUNCS(DECLARE)
DEVICE_FUNCS(DECLARE)
static PFN_vkCreateInstance vkCreateInstance;

/* ------------------------------------------------------------- helpers */

#define LOG(...)                                                                                   \
   do {                                                                                            \
      fprintf(stderr, "vk-triangle: " __VA_ARGS__);                                                \
      fputc('\n', stderr);                                                                         \
      fflush(stderr);                                                                              \
   } while (0)

#define CHECK(call)                                                                                \
   do {                                                                                            \
      VkResult check_result_ = (call);                                                             \
      if (check_result_ != VK_SUCCESS) {                                                           \
         LOG("%s failed: %d (line %d)", #call, (int)check_result_, __LINE__);                      \
         exit(1);                                                                                  \
      }                                                                                            \
   } while (0)

static double
now_seconds(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static VkShaderModule
create_shader(VkDevice device, const uint32_t *code, size_t size)
{
   const VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = code,
   };
   VkShaderModule module;
   CHECK(vkCreateShaderModule(device, &info, NULL, &module));
   return module;
}

static void
image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
              VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access)
{
   const VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src_stage,
      .srcAccessMask = src_access,
      .dstStageMask = dst_stage,
      .dstAccessMask = dst_access,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   const VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
   };
   vkCmdPipelineBarrier2(cmd, &dependency);
}

/* ---------------------------------------------------------------- main */
#include <fcntl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
   const double run_seconds = argc > 1 ? atof(argv[1]) : 30.0;
   int fd = open("/dev/console", O_WRONLY);
   dup2(fd, 1);
   dup2(fd, 2);


   /* Instance: Vulkan 1.3, presenting to a display. */
   vkCreateInstance = (PFN_vkCreateInstance)get_instance_proc_addr(NULL, "vkCreateInstance");
   if (!vkCreateInstance) {
      LOG("no vkCreateInstance from the driver");
      return 1;
   }

   const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                        VK_KHR_DISPLAY_EXTENSION_NAME};
   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "ps5-vk-triangle",
      .apiVersion = VK_API_VERSION_1_3,
   };
   const VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
      .enabledExtensionCount = 2,
      .ppEnabledExtensionNames = instance_extensions,
   };
   VkInstance instance;
   CHECK(vkCreateInstance(&instance_info, NULL, &instance));

#define LOAD_INSTANCE(name)                                                                        \
   name = (PFN_##name)get_instance_proc_addr(instance, #name);                                     \
   if (!name) {                                                                                    \
      LOG("missing %s", #name);                                                                    \
      return 1;                                                                                    \
   }
   INSTANCE_FUNCS(LOAD_INSTANCE)

   /* The GPU. */
   uint32_t count = 1;
   VkPhysicalDevice gpu;
   VkResult result = vkEnumeratePhysicalDevices(instance, &count, &gpu);
   if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
      LOG("no physical device (%d)", (int)result);
      return 1;
   }
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(gpu, &props);
   LOG("device: %s, Vulkan %u.%u.%u", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
       VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));

   /* The display and its mode: the 59.94 Hz one where there is a choice
    * (119.88 Hz needs the title's high-frame-rate declaration). */
   count = 1;
   VkDisplayPropertiesKHR display_props;
   result = vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, &display_props);
   if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
      LOG("no display (%d)", (int)result);
      return 1;
   }
   const VkDisplayKHR display = display_props.display;

   VkDisplayModePropertiesKHR modes[8];
   count = 8;
   result = vkGetDisplayModePropertiesKHR(gpu, display, &count, modes);
   if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
      LOG("no display mode (%d)", (int)result);
      return 1;
   }
   uint32_t mode_index = 0;
   for (uint32_t i = 0; i < count; i++) {
      if (modes[i].parameters.refreshRate <= 60000) {
         mode_index = i;
         break;
      }
   }
   const VkDisplayModePropertiesKHR mode = modes[mode_index];
   LOG("display: %ux%u at %.2f Hz", mode.parameters.visibleRegion.width,
       mode.parameters.visibleRegion.height, mode.parameters.refreshRate / 1000.0);

   /* Plane 0, opaque. */
   VkDisplayPlaneCapabilitiesKHR plane_caps;
   CHECK(vkGetDisplayPlaneCapabilitiesKHR(gpu, mode.displayMode, 0, &plane_caps));
   VkDisplayPlaneAlphaFlagBitsKHR alpha = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
   if (!(plane_caps.supportedAlpha & alpha))
      alpha = (VkDisplayPlaneAlphaFlagBitsKHR)(plane_caps.supportedAlpha & -plane_caps.supportedAlpha);

   const VkDisplaySurfaceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
      .displayMode = mode.displayMode,
      .planeIndex = 0,
      .planeStackIndex = 0,
      .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .globalAlpha = 1.0f,
      .alphaMode = alpha,
      .imageExtent = mode.parameters.visibleRegion,
   };
   VkSurfaceKHR surface;
   CHECK(vkCreateDisplayPlaneSurfaceKHR(instance, &surface_info, NULL, &surface));

   /* A graphics queue that can present to it. */
   VkQueueFamilyProperties families[8];
   count = 8;
   vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
   uint32_t family = UINT32_MAX;
   for (uint32_t i = 0; i < count && family == UINT32_MAX; i++) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
         family = i;
   }
   if (family == UINT32_MAX) {
      LOG("no graphics queue that presents");
      return 1;
   }

   /* Device: swapchains, and the 1.3 features used below. */
   const float priority = 1.0f;
   const VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
   };
   VkPhysicalDeviceVulkan13Features features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE,
   };
   const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
   const VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features13,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = 1,
      .ppEnabledExtensionNames = device_extensions,
   };
   VkDevice device;
   CHECK(vkCreateDevice(gpu, &device_info, NULL, &device));

#define LOAD_DEVICE(name)                                                                          \
   name = (PFN_##name)vkGetDeviceProcAddr(device, #name);                                          \
   if (!name) {                                                                                    \
      LOG("missing %s", #name);                                                                    \
      return 1;                                                                                    \
   }
   DEVICE_FUNCS(LOAD_DEVICE)

   VkQueue queue;
   vkGetDeviceQueue(device, family, 0, &queue);

   /* Swapchain: an 8-bit BGRA/RGBA format, FIFO (the only mode). */
   VkSurfaceCapabilitiesKHR caps;
   CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
   VkSurfaceFormatKHR formats[16];
   count = 16;
   result = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats);
   if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0) {
      LOG("no surface format (%d)", (int)result);
      return 1;
   }
   VkSurfaceFormatKHR format = formats[0];
   for (uint32_t i = 0; i < count; i++) {
      if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
          formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
         format = formats[i];
         break;
      }
   }

   VkExtent2D extent = caps.currentExtent;
   if (extent.width == UINT32_MAX)
      extent = mode.parameters.visibleRegion;

   uint32_t image_count = caps.minImageCount + 1;
   if (caps.maxImageCount && image_count > caps.maxImageCount)
      image_count = caps.maxImageCount;
   if (image_count > MAX_IMAGES)
      image_count = MAX_IMAGES;

   VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   if (!(caps.supportedCompositeAlpha & composite))
      composite = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha &
                                                -caps.supportedCompositeAlpha);

   const VkSwapchainCreateInfoKHR swapchain_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = image_count,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = caps.currentTransform,
      .compositeAlpha = composite,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
   };
   VkSwapchainKHR swapchain;
   CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));

   VkImage images[MAX_IMAGES];
   image_count = MAX_IMAGES;
   result = vkGetSwapchainImagesKHR(device, swapchain, &image_count, images);
   if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
      LOG("vkGetSwapchainImagesKHR failed: %d", (int)result);
      return 1;
   }
   LOG("swapchain: %u images, %ux%u, format %d", image_count, extent.width, extent.height,
       (int)format.format);

   VkImageView views[MAX_IMAGES];
   VkSemaphore render_done[MAX_IMAGES]; /* one per image: reused only after its present */
   const VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
   for (uint32_t i = 0; i < image_count; i++) {
      const VkImageViewCreateInfo view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = images[i],
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = format.format,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
      };
      CHECK(vkCreateImageView(device, &view_info, NULL, &views[i]));
      CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &render_done[i]));
   }

   /* Pipeline: no vertex input, dynamic viewport, dynamic rendering. */
   const VkPushConstantRange push_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(float)};
   const VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_range,
   };
   VkPipelineLayout layout;
   CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));

   VkShaderModule vert = create_shader(device, triangle_vert, sizeof(triangle_vert));
   VkShaderModule frag = create_shader(device, triangle_frag, sizeof(triangle_frag));
   const VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT,
       vert, "main", NULL},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
       frag, "main", NULL},
   };
   const VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };
   const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };
   const VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
   };
   const VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   const VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   const VkPipelineColorBlendAttachmentState blend_attachment = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
   };
   const VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blend_attachment,
   };
   const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   const VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
   };
   const VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &format.format,
   };
   const VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_info,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = layout,
   };
   VkPipeline pipeline;
   const double compile_start = now_seconds();
   CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
   LOG("pipeline compiled in %.0f ms", (now_seconds() - compile_start) * 1000.0);
   vkDestroyShaderModule(device, vert, NULL);
   vkDestroyShaderModule(device, frag, NULL);

   /* Per frame in flight: a command buffer, its fence, its acquire semaphore. */
   const VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = family,
   };
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));

   VkCommandBuffer cmds[FRAMES_IN_FLIGHT];
   const VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = FRAMES_IN_FLIGHT,
   };
   CHECK(vkAllocateCommandBuffers(device, &alloc_info, cmds));

   VkFence fences[FRAMES_IN_FLIGHT];
   VkSemaphore acquired[FRAMES_IN_FLIGHT];
   const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
   };
   for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
      CHECK(vkCreateFence(device, &fence_info, NULL, &fences[i]));
      CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired[i]));
   }

   /* Draw until the time is up. */
   const float aspect = (float)extent.width / (float)extent.height;
   const double start = now_seconds();
   double last_report = start;
   uint64_t frame = 0, frames_since_report = 0;

   for (;;) {
      const double t = now_seconds() - start;
      if (run_seconds > 0.0 && t >= run_seconds)
         break;

      const uint32_t f = frame % FRAMES_IN_FLIGHT;
      CHECK(vkWaitForFences(device, 1, &fences[f], VK_TRUE, UINT64_MAX));

      uint32_t index;
      result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired[f], VK_NULL_HANDLE,
                                     &index);
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
         LOG("vkAcquireNextImageKHR failed: %d", (int)result);
         break;
      }
      CHECK(vkResetFences(device, 1, &fences[f]));

      VkCommandBuffer cmd = cmds[f];
      CHECK(vkResetCommandBuffer(cmd, 0));
      const VkCommandBufferBeginInfo begin = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };
      CHECK(vkBeginCommandBuffer(cmd, &begin));

      image_barrier(cmd, images[index], VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

      /* A slow pulse between dark blue and dark purple. */
      const float pulse = 0.5f + 0.5f * sinf((float)t * 1.5f);
      const VkRenderingAttachmentInfo color = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = views[index],
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue.color = {{0.05f + 0.15f * pulse, 0.05f, 0.20f, 1.0f}},
      };
      const VkRenderingInfo rendering = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
         .renderArea = {{0, 0}, extent},
         .layerCount = 1,
         .colorAttachmentCount = 1,
         .pColorAttachments = &color,
      };
      vkCmdBeginRendering(cmd, &rendering);

      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      const VkViewport viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height,
                                   0.0f, 1.0f};
      const VkRect2D scissor = {{0, 0}, extent};
      vkCmdSetViewport(cmd, 0, 1, &viewport);
      vkCmdSetScissor(cmd, 0, 1, &scissor);
      const float push[2] = {(float)t, aspect};
      vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
      vkCmdDraw(cmd, 3, 1, 0, 0);

      vkCmdEndRendering(cmd);

      image_barrier(cmd, images[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    /* The stage the semaphore below signals at, so the
                     * transition is ordered before the present. */
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
      CHECK(vkEndCommandBuffer(cmd));

      const VkSemaphoreSubmitInfo wait = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = acquired[f],
         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      };
      const VkSemaphoreSubmitInfo signal = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = render_done[index],
         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      };
      const VkCommandBufferSubmitInfo cmd_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = cmd,
      };
      const VkSubmitInfo2 submit = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
         .waitSemaphoreInfoCount = 1,
         .pWaitSemaphoreInfos = &wait,
         .commandBufferInfoCount = 1,
         .pCommandBufferInfos = &cmd_info,
         .signalSemaphoreInfoCount = 1,
         .pSignalSemaphoreInfos = &signal,
      };
      CHECK(vkQueueSubmit2(queue, 1, &submit, fences[f]));

      const VkPresentInfoKHR present = {
         .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
         .waitSemaphoreCount = 1,
         .pWaitSemaphores = &render_done[index],
         .swapchainCount = 1,
         .pSwapchains = &swapchain,
         .pImageIndices = &index,
      };
      result = vkQueuePresentKHR(queue, &present);
      if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
         LOG("vkQueuePresentKHR failed: %d", (int)result);
         break;
      }

      frame++;
      frames_since_report++;
      const double now = now_seconds();
      if (now - last_report >= 5.0) {
         LOG("%.1f fps", frames_since_report / (now - last_report));
         last_report = now;
         frames_since_report = 0;
      }
   }

   LOG("%llu frames, exiting", (unsigned long long)frame);

   /* Clean up. */
   vkDeviceWaitIdle(device);
   for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
      vkDestroySemaphore(device, acquired[i], NULL);
      vkDestroyFence(device, fences[i], NULL);
   }
   vkDestroyCommandPool(device, pool, NULL);
   vkDestroyPipeline(device, pipeline, NULL);
   vkDestroyPipelineLayout(device, layout, NULL);
   for (uint32_t i = 0; i < image_count; i++) {
      vkDestroySemaphore(device, render_done[i], NULL);
      vkDestroyImageView(device, views[i], NULL);
   }
   vkDestroySwapchainKHR(device, swapchain, NULL);
   vkDestroyDevice(device, NULL);
   vkDestroySurfaceKHR(instance, surface, NULL);
   vkDestroyInstance(instance, NULL);
   return 0;
}
