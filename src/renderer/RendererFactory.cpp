#include "RendererFactory.h"
#include "CPURenderer.h"
#include "GLVideoRenderer.h"
#include "YUVVideoRenderer.h"

std::unique_ptr<Renderer> RendererFactory::Create(RendererType type)
{
    switch (type)
    {
    case RendererType::CPU:
        return std::make_unique<CPURenderer>();
    case RendererType::OpenGL:
        return std::make_unique<GLVideoRenderer>();
    case RendererType::OpenGLYUV:
        return std::make_unique<YUVVideoRenderer>();
    default:
        return std::make_unique<CPURenderer>();
    }
}
