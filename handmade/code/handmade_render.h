#if !defined(HANDMADE_RENDER_H)
/*
  NOTE(yigit): The OpenGL BACKEND.  It reads the command buffer that
  handmade_renderer.h defines and turns each command into GL calls.

  This is the only file on the game side that still knows OpenGL exists.  When
  a Vulkan backend arrives it will be a second file beside this one,
  implementing the same one function against the same command stream - and
  nothing in handmade.cpp will change.

  Everything here is per frame.  Loading assets is handmade_assets.h.
*/

// Everything a GL backend needs to execute a command, gathered so it is one
// parameter rather than three.
//
// NOTE(yigit): Programs is indexed by render_program, NOT by the raw
// ShaderProgram array.  The mapping from role to handle lives here on purpose:
// a GLuint in the command stream would be OpenGL leaking into a file that is
// supposed to have none.
struct opengl_backend
{
    game_opengl_api *GL;
    uint32 Programs[RenderProgram_Count];
    uint32 WhiteTexture;
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
                      render_point_light *Light, vec3 PositionView)
{
    char Name[64];

    snprintf(Name, sizeof(Name), "pointLights[%u].position", Index);
    SetUniformVec3(GL, Program, Name, PositionView);

    snprintf(Name, sizeof(Name), "pointLights[%u].constant", Index);
    SetUniformFloat(GL, Program, Name, Light->Constant);
    snprintf(Name, sizeof(Name), "pointLights[%u].linear", Index);
    SetUniformFloat(GL, Program, Name, Light->Linear);
    snprintf(Name, sizeof(Name), "pointLights[%u].quadratic", Index);
    SetUniformFloat(GL, Program, Name, Light->Quadratic);

    snprintf(Name, sizeof(Name), "pointLights[%u].ambient", Index);
    SetUniformVec3(GL, Program, Name, Light->Ambient);
    snprintf(Name, sizeof(Name), "pointLights[%u].diffuse", Index);
    SetUniformVec3(GL, Program, Name, Light->Diffuse);
    snprintf(Name, sizeof(Name), "pointLights[%u].specular", Index);
    SetUniformVec3(GL, Program, Name, Light->Specular);
}

/*
  Uploads one Setup command to the lit program.

  NOTE(yigit): Every value now arrives in the command rather than from a global
  in this file.  That is the actual change the push buffer makes - the scene
  describes its lights, and the backend only decides how to express them.

  The world-to-view conversion stays here on purpose: which space this renderer
  lights in is a rendering decision, not something the scene should have to
  know about.
*/
internal void
SetLitUniforms(game_opengl_api *GL, uint32 Program, render_command_setup *Setup)
{
    SetUniformMat4(GL, Program, "view", Setup->View);
    SetUniformMat4(GL, Program, "projection", Setup->Projection);

    // NOTE(yigit): material.shininess, diffuseColor and specularColor are NOT
    // set here - they vary per submesh, so GameDrawModel sets them immediately
    // before each draw.

    // The SPOTLIGHT - the flashlight held at the camera.  It needs no position
    // or direction uniform: in view space the camera is the origin looking down
    // -Z, so the shader has both as constants.
    SetUniformVec3(GL, Program, "spotLight.ambient",  Setup->SpotAmbient);
    SetUniformVec3(GL, Program, "spotLight.diffuse",  Setup->SpotDiffuse);
    SetUniformVec3(GL, Program, "spotLight.specular", Setup->SpotSpecular);

    SetUniformFloat(GL, Program, "spotLight.constant",  Setup->SpotConstant);
    SetUniformFloat(GL, Program, "spotLight.linear",    Setup->SpotLinear);
    SetUniformFloat(GL, Program, "spotLight.quadratic", Setup->SpotQuadratic);

    // Already cosines by the time they arrive - the scene converts from degrees
    // once, rather than this doing it every frame.
    SetUniformFloat(GL, Program, "spotLight.cutOff",      Setup->SpotCutOff);
    SetUniformFloat(GL, Program, "spotLight.outerCutOff", Setup->SpotOuterCutOff);

    // Book ch. 17.1 - the directional light.
    //
    // NOTE(yigit): W is 0, not 1.  A direction has no location, so the view
    // matrix's translation column must not touch it.
    vec4 DirView = Setup->View * Vec4(Setup->DirLightDirectionWorld, 0.0f);
    SetUniformVec3(GL, Program, "dirLight.direction",
                   Vec3(DirView.X, DirView.Y, DirView.Z));

    SetUniformVec3(GL, Program, "dirLight.ambient",  Setup->DirAmbient);
    SetUniformVec3(GL, Program, "dirLight.diffuse",  Setup->DirDiffuse);
    SetUniformVec3(GL, Program, "dirLight.specular", Setup->DirSpecular);

    // Book ch. 17.2 - the point lights.
    //
    // NOTE(yigit): W is 1 here, not 0.  A position DOES get slid by the view
    // matrix's translation - that is the entire difference from the direction
    // above, and getting it backwards is the classic way to end up with lights
    // that drift as the camera moves.
    for(uint32 LightIndex = 0;
        LightIndex < Setup->PointLightCount;
        ++LightIndex)
    {
        render_point_light *Light = Setup->PointLights + LightIndex;
        vec4 PosView = Setup->View * Vec4(Light->PositionWorld, 1.0f);

        SetPointLightUniforms(GL, Program, LightIndex, Light,
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
SetLampUniforms(game_opengl_api *GL, uint32 Program, render_command_setup *Setup)
{
    SetUniformMat4(GL, Program, "view", Setup->View);
    SetUniformMat4(GL, Program, "projection", Setup->Projection);

    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, Program, "LightColor", Setup->SpotSpecular, 1.0f);
}

/*
  Walks the command buffer and executes it.  The entire backend is this one
  function plus the helpers above.

  NOTE(yigit): The walk steps by Header->Size, not by the sizeof of whatever
  type the switch matched.  That is what lets a command this backend does not
  recognise be skipped rather than desynchronising the whole stream.
*/
internal void
RenderBufferExecute(opengl_backend *Backend, render_buffer *Buffer)
{
    game_opengl_api *GL = Backend->GL;

    // Uniforms shared by a whole frame arrive in a Setup command, and the draws
    // that follow need them.  Remembered rather than re-read, so a draw never
    // has to search backwards through the buffer.
    render_command_setup *Setup = 0;

    uint8 *At = Buffer->Base;
    uint8 *End = Buffer->Base + Buffer->Used;

    while(At < End)
    {
        render_command_header *Header = (render_command_header *)At;

        switch(Header->Type)
        {
            case RenderCommand_Clear:
            {
                render_command_clear *Command = (render_command_clear *)Header;
                GL->glClearColor(Command->Color.X, Command->Color.Y,
                                 Command->Color.Z, Command->Color.W);
                GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            } break;

            case RenderCommand_Setup:
            {
                Setup = (render_command_setup *)Header;

                // Both programs are told, and neither can be told lazily -
                // uniforms belong to a program, not to the context.
                SetLitUniforms(GL, Backend->Programs[RenderProgram_Lit], Setup);
                SetLampUniforms(GL, Backend->Programs[RenderProgram_Lamp], Setup);
            } break;

            case RenderCommand_DrawModel:
            {
                render_command_draw_model *Command = (render_command_draw_model *)Header;

                uint32 Program = Backend->Programs[Command->Program];
                GameDrawModel(GL, Program, Command->Model, Command->Transform,
                              Backend->WhiteTexture);
            } break;

            case RenderCommand_DrawOverlay:
            {
                render_command_draw_overlay *Command = (render_command_draw_overlay *)Header;

                // NOTE(yigit): OverlayFlush owns the blend and depth state it
                // needs, and restores it afterwards.  That bookkeeping used to
                // sit in the game layer; it belongs on this side of the seam,
                // because which state a 2D pass requires is an api question.
                OverlayFlush(Command->Overlay, GL,
                             Backend->Programs[RenderProgram_Overlay],
                             Command->Projection,
                             Command->Font ? Command->Font->Texture : Backend->WhiteTexture,
                             Command->Color);
            } break;
        }

        At += Header->Size;
    }

    GL->glBindVertexArray(0);
}

#define HANDMADE_RENDER_H
#endif
