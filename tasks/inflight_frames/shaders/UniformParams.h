#ifndef UNIFORM_PARAMS_H_INCLUDED
#define UNIFORM_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct UniformParams
{
  shader_vec2 resolution;
  shader_vec2 mouse;
  shader_float time;
};


#endif // UNIFORM_PARAMS_H_INCLUDED
