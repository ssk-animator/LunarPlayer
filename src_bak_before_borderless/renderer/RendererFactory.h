#ifndef RENDERERFACTORY_H
#define RENDERERFACTORY_H

#include <memory>

class Renderer;

enum class RendererType
{
    CPU,
    OpenGL,
    OpenGLYUV
};

class RendererFactory
{
public:
    static std::unique_ptr<Renderer> Create(RendererType type);
};

#endif
