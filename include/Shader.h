#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Shader — thin RAII wrapper around an OpenGL shader program.
//
// Design:
//   Takes vertex and fragment source strings at construction, compiles both
//   shaders, links them into a program, and reports any errors to stderr.
//   Provides typed `set*` helpers so callers never have to call
//   glGetUniformLocation themselves.  The program ID is public so the caller
//   can pass it to glDeleteProgram at teardown if needed.
// ─────────────────────────────────────────────────────────────────────────────
class Shader {
public:
    GLuint id;  // OpenGL program object handle

    // Compile `vertSrc` and `fragSrc`, link into a program.
    Shader(const char* vertSrc, const char* fragSrc) {
        GLuint vert = compile(GL_VERTEX_SHADER,   vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);
        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);
        checkLink();
        // Shaders are no longer needed once linked into the program.
        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() const { glUseProgram(id); }

    // ── Uniform setters ──────────────────────────────────────────────────────

    void setInt(const char* name, int v) const {
        glUniform1i(loc(name), v);
    }
    void setFloat(const char* name, float v) const {
        glUniform1f(loc(name), v);
    }
    // Upload a 3-component float vector.
    void setVec3(const char* name, const glm::vec3& v) const {
        glUniform3fv(loc(name), 1, glm::value_ptr(v));
    }
    // Upload a column-major 4×4 matrix (GL_FALSE = do not transpose,
    // because GLM already stores in column-major order matching OpenGL).
    void setMat4(const char* name, const glm::mat4& m) const {
        glUniformMatrix4fv(loc(name), 1, GL_FALSE, glm::value_ptr(m));
    }

private:
    // Look up a uniform location. Returns -1 (silently ignored by OpenGL) if
    // the uniform is optimised away or misspelled.
    GLint loc(const char* name) const {
        return glGetUniformLocation(id, name);
    }

    // Compile a single shader stage and return its handle.
    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << "[Shader compile error] " << log << '\n';
        }
        return s;
    }

    // Check program link status after glLinkProgram.
    void checkLink() const {
        int ok = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(id, 512, nullptr, log);
            std::cerr << "[Shader link error] " << log << '\n';
        }
    }
};
