#if !defined(HANDMADE_RENDER_H)
/*
  NOTE(yigit): The per-frame drawing side - what the lights are, what the
  uniforms for each program are, and how one model gets drawn.  Split out of
  handmade.cpp alongside handmade_assets.h.

  The line between this file and that one is WHEN the code runs: assets load
  once at startup, this runs every frame.

  NOT a renderer abstraction.  These functions still call OpenGL directly and
  still take a game_opengl_api - that is the seam the push buffer will cut
  later, and moving files around does not cut it.
*/

// Book ch. 17.1 - the sun.  World space, pointing INTO the scene: down and
// slightly back-left, so it lights the tops of the containers.
global_variable const vec3 GlobalDirLightDirection = {-0.2f, -1.0f, -0.3f};

// Book ch. 17.3, p. 176 - four point lights scattered among the containers.
// WORLD space; SetPointLightUniforms converts each one to view space.  A lamp
// marker is drawn at each, so what you see is where the light is.
global_variable const vec3 GlobalPointLightPositions[] =
{
    { 0.7f,  0.2f,   2.0f},
    { 2.3f, -3.3f,  -4.0f},
    {-4.0f,  2.0f, -12.0f},
    { 0.0f,  0.0f,  -3.0f},
};

// Draws one loaded model, a submesh at a time, setting that submesh's material
// before each call.
//
// NOTE(yigit): One glDrawElements per material rather than one per model.  A
// material change is a uniform change, and uniforms cannot vary within a draw -
// which is the whole reason the index buffer was reordered by material.
internal void
GameDrawModel(game_opengl_api *GL, uint32 Program, render_model *Model,
              mat4 ModelMatrix, uint32 WhiteTexture)
{
    if(!Model->IndexCount)
    {
        return;
    }

    // The At-setters below write to whatever program is bound, so this has to
    // happen before any of them - and it replaces the glUseProgram the
    // name-based setters used to do on every single write.
    GL->glUseProgram(Program);
    GL->glBindVertexArray(Model->VAO);

    // NOTE(yigit): Hoisted out of the loop.  A uniform's location is fixed for
    // the life of a linked program, and these four do not change between the
    // submeshes below - so a model with 24 materials goes from 96 driver string
    // lookups a frame to 4.
    //
    // Looked up per frame rather than cached in game_state on purpose: shader
    // hot reload relinks the program, and every location from the old one is
    // then meaningless.
    int32 ModelLocation         = GetUniformLocation(GL, Program, "model");
    int32 DiffuseColorLocation  = GetUniformLocation(GL, Program, "material.diffuseColor");
    int32 SpecularColorLocation = GetUniformLocation(GL, Program, "material.specularColor");
    int32 ShininessLocation     = GetUniformLocation(GL, Program, "material.shininess");

    SetUniformMat4At(GL, ModelLocation, ModelMatrix);

    for(uint32 SubmeshIndex = 0;
        SubmeshIndex < Model->SubmeshCount;
        ++SubmeshIndex)
    {
        render_submesh *Submesh = Model->Submeshes + SubmeshIndex;
        obj_material *Material = &Submesh->Material;

        SetUniformVec3At(GL, DiffuseColorLocation, Material->Diffuse);
        SetUniformVec3At(GL, SpecularColorLocation, Material->Specular);
        SetUniformFloatAt(GL, ShininessLocation, Material->Shininess);

        // White when the material named no texture, so the multiply in the
        // shader leaves Kd untouched.
        uint32 DiffuseTexture = Submesh->DiffuseTexture
            ? Submesh->DiffuseTexture
            : WhiteTexture;

        uint32 AlphaTexture = Submesh->AlphaTexture
            ? Submesh->AlphaTexture
            : WhiteTexture;

        GL->glActiveTexture(GL_TEXTURE0);
        GL->glBindTexture(GL_TEXTURE_2D, DiffuseTexture);
        GL->glActiveTexture(GL_TEXTURE1);
        GL->glBindTexture(GL_TEXTURE_2D, WhiteTexture);
        GL->glActiveTexture(GL_TEXTURE2);
        GL->glBindTexture(GL_TEXTURE_2D, AlphaTexture);

        // The last argument is a byte OFFSET into the bound element buffer,
        // not a pointer - a leftover from when this call could read indices
        // straight out of client memory.  It was 0 while there was one draw
        // per model; now it is where this material's run begins.
        GL->glDrawElements(GL_TRIANGLES, (int32)Submesh->IndexCount, GL_UNSIGNED_INT,
                           (void *)(memory_index)(Submesh->FirstIndex * sizeof(uint32)));
    }
}

// Book ch. 17.3 - the uniform names here are "pointLights[2].quadratic" and
// friends.  The book builds them with std::string concatenation; with no
// std::string in this layer they are formatted into a scratch buffer instead.
//
// NOTE(yigit): A local buffer is safe because glGetUniformLocation copies the
// name it is handed - nothing keeps a pointer to it after the call returns.
internal void
SetPointLightUniforms(game_opengl_api *GL, uint32 Program, uint32 Index,
                      vec3 PositionView)
{
    char Name[64];

    snprintf(Name, sizeof(Name), "pointLights[%u].position", Index);
    SetUniformVec3(GL, Program, Name, PositionView);

    // Book ch. 16.2 - the table row for a range of about 50 units.
    snprintf(Name, sizeof(Name), "pointLights[%u].constant", Index);
    SetUniformFloat(GL, Program, Name, 1.0f);
    snprintf(Name, sizeof(Name), "pointLights[%u].linear", Index);
    SetUniformFloat(GL, Program, Name, 0.09f);
    snprintf(Name, sizeof(Name), "pointLights[%u].quadratic", Index);
    SetUniformFloat(GL, Program, Name, 0.032f);

    snprintf(Name, sizeof(Name), "pointLights[%u].ambient", Index);
    SetUniformVec3(GL, Program, Name, 0.05f, 0.05f, 0.05f);
    snprintf(Name, sizeof(Name), "pointLights[%u].diffuse", Index);
    SetUniformVec3(GL, Program, Name, 0.8f, 0.8f, 0.8f);
    snprintf(Name, sizeof(Name), "pointLights[%u].specular", Index);
    SetUniformVec3(GL, Program, Name, 1.0f, 1.0f, 1.0f);
}

internal void
SetLitUniforms(game_opengl_api *GL, uint32 Program,
               mat4 View, mat4 Projection,
               vec3 AmbientColor, vec3 DiffuseColor)
{
    SetUniformMat4(GL, Program, "view", View);
    SetUniformMat4(GL, Program, "projection", Projection);

    // NOTE(yigit): material.shininess, diffuseColor and specularColor are NOT
    // set here any more - they vary per submesh, so GameDrawModel sets them
    // immediately before each draw.

    // The SPOTLIGHT - the flashlight held at the camera.  It needs no position
    // or direction uniform: in view space the camera is the origin looking down
    // -Z, so the shader has both as constants.
    SetUniformVec3(GL, Program, "spotLight.ambient",  AmbientColor);
    SetUniformVec3(GL, Program, "spotLight.diffuse",  DiffuseColor);
    SetUniformVec3(GL, Program, "spotLight.specular", 1.0f, 1.0f, 1.0f);

    // Book ch. 16.2 - the table row for a range of about 50 units.  The
    // 3250-unit row (0.0014 / 0.000007) gives no visible falloff in a scene
    // this small.
    SetUniformFloat(GL, Program, "spotLight.constant", 1.0f);
    SetUniformFloat(GL, Program, "spotLight.linear", 0.09f);
    SetUniformFloat(GL, Program, "spotLight.quadratic", 0.032f);

    // Cos takes RADIANS.  Passing 12.5 raw is 12.5 radians, which works out as
    // a 3.8 degree cone - wrong, but close enough to look plausible.
    SetUniformFloat(GL, Program, "spotLight.cutOff", Cos(12.5f*Pi32 / 180.0f));
    SetUniformFloat(GL, Program, "spotLight.outerCutOff", Cos(17.5f*Pi32 / 180.0f));

    // Book ch. 17.1 - the directional light.  Its direction is given in WORLD
    // space and has to reach the shader in VIEW space, like everything else in
    // this pipeline.
    //
    // NOTE(yigit): W is 0, not 1.  A direction has no location, so the view
    // matrix's translation column must not touch it - the same reason the
    // vertex shader used mat3(view) back when this lived there.
    vec4 DirView = View * Vec4(GlobalDirLightDirection, 0.0f);
    SetUniformVec3(GL, Program, "dirLight.direction",
                   Vec3(DirView.X, DirView.Y, DirView.Z));

    SetUniformVec3(GL, Program, "dirLight.ambient",  0.05f, 0.05f, 0.05f);
    SetUniformVec3(GL, Program, "dirLight.diffuse",  0.4f,  0.4f,  0.4f);
    SetUniformVec3(GL, Program, "dirLight.specular", 0.5f,  0.5f,  0.5f);

    // Book ch. 17.2 - the four point lights.
    //
    // NOTE(yigit): W is 1 here, not 0.  A position DOES get slid by the view
    // matrix's translation - that is the entire difference from the direction
    // above, and getting it backwards is the classic way to end up with lights
    // that drift as the camera moves.
    for(uint32 LightIndex = 0;
        LightIndex < ArrayCount(GlobalPointLightPositions);
        ++LightIndex)
    {
        vec4 PosView = View * Vec4(GlobalPointLightPositions[LightIndex], 1.0f);
        SetPointLightUniforms(GL, Program, LightIndex,
                              Vec3(PosView.X, PosView.Y, PosView.Z));
    }


    // Which texture unit each sampler reads from.  Constant, but uniforms do
    // not survive a program rebuild, so they are re-sent every frame like the
    // rest.
    SetUniformInt(GL, Program, "material.diffuse", 0);
    SetUniformInt(GL, Program, "material.specular", 1);
    SetUniformInt(GL, Program, "material.alphaMask", 2);
}

// NOTE(yigit): View and projection have to be set here as well as on the lit
// program.  Uniforms belong to a program, not to the context - the ones set
// over there simply do not exist in this one.
internal void
SetLampUniforms(game_opengl_api *GL, uint32 Program,
                mat4 View, mat4 Projection, vec3 LightColor)
{
    SetUniformMat4(GL, Program, "view", View);
    SetUniformMat4(GL, Program, "projection", Projection);

    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, Program, "LightColor", LightColor, 1.0f);
}

#define HANDMADE_RENDER_H
#endif
