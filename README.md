<img src="SlimTracinCpp_logo.png" alt="SlimTracinCPP_logo"><br>

<img src="src/examples/GPU.gif" alt="GPU"><br>

A minimalist and platform-agnostic interactive/real-time raytracer written in C++.<br>
Strong emphasis on simplicity, ease of use and almost no setup to get started with.<br>  

This project extends [SlimEngine++](https://github.com/HardCoreCodin/SlimEngineCpp).

Optional GPU support is provided via CUDA.<br>
The same C++ code is cross-compiled (no CUDA libraries used).<br>
Compiling using CUDA allows for dynamic toggling between rendering on the CPU or the GPU.<br>
<img src="src/examples/XPU.gif" alt="XPU">
<img src="src/examples/XPU.png" alt="XPU" width="540">

Architecture:
-
The platform layer only uses operating-system headers (no standard library used).<br>
The application layer itself (non-CUDA) has no dependencies, apart from the standard math header.<br>
It is just a library that the platform layer uses - it has no knowledge of the platform.<br>

More details on this architecture [here](https://youtu.be/Ev_TeQmus68).

Features:
-
All features of <b>SlimEngine++</b> are available here as well.<br>
Additional features include raytracing facilities:<br>
- Acceleration Structure (BVH) construction and traversal
- Intersection shaders for triangular meshes and implicit geometry
- Debug render modes (Depth, Normal, UV and BVH-preview)
- Physically based materials (Micro-facet Cook-Torrance BRDF)
- Raytracing specific shaders (Glass, Mirror, Area lights)
- Textures (Bi-Linear filtered) with adaptive mip-selection (using ray cones)
<img src="src/examples/RayCones.gif" alt="RayCones" width="540"><br>

The following example apps demonstrate how to use <b>SlimTracin</b>'s features:<br>
<i>Note: Each example comes with CMake targets for CPU-only (no CUDA required) or GPU-enabled (requiring CUDA)</i><br>
* <b><u>Implicit Geometry</b>:</u> Quad, Box, Sphere and Tetrahedra, all with UV-based transparency<br>
  <img src="src/examples/02_Geometry.gif" alt="02_Geometry"><br>
  <img src="src/examples/02_Geometry_setup.png" alt="02_Geometry_setup" width="540">
  <img src="src/examples/02_Geometry_transparency.png" alt="02_Geometry_transparency" width="540">
* <b><u>Point lights</b>:</u> Can be moved around and scaled (changing their light intensity)<br>
  <img src="src/examples/01_Lights.gif" alt="01_Lights"><br>
  <img src="src/examples/01_Lights_setup.png" alt="01_Lights_setup" width="540">
* <b><u>Area Lights</b>:</u> Emissive quads can be used as rectangular area lights<br>
  <img src="src/examples/06_AreaLights.gif" alt="06_AreaLights"><br>
  <p float="left">   
    <img src="src/examples/06_AreaLights_setup.png" alt="06_AreaLights_setup" width="380">
    <img src="src/examples/06_AreaLights_selection.png" alt="06_AreaLights_selection" width="360">
  </p>  
* <b><u>Classic Materials</b>:</u> Lambert, Blinn, Phong<br>
  <img src="src/examples/03_BlinnPhong.gif" alt="03_BlinnPhong"><br>
  <p float="left">
    <img src="src/examples/03_BlinnPhong_setup.png" alt="03_BlinnPhong_setup" width="360">
    <img src="src/examples/03_BlinnPhong_selection.png" alt="03_BlinnPhong_selection" width="380">
  </p>
* <b><u>PBR Materials</b>:</u> Cook-Torrance BRDF (Schlick/Smith GGX)<br>
  <img src="src/examples/05_PBR.gif" alt="05_PBR"><br>
  <img src="src/examples/05_PBR.png" alt="05_PBR" width="540">
* <b><u>Reflective/Refractive Materials</b>:</u> For glasses and mirrors (bounce count is controlled globally)<br>
  <img src="src/examples/04_GlassMirror.gif" alt="04_GlassMirror"><br>
  <img src="src/examples/04_GlassMirror_WithNormals.gif" alt="04_GlassMirror_Bumpy"><br>
  <p float="left">   
    <img src="src/examples/04_GlassMirror_setup.png" alt="04_GlassMirror_setup" width="360">
    <img src="src/examples/04_GlassMirror_selection.png" alt="04_GlassMirror_selection" width="380">
  </p>
* <b><u>Meshes</b>:</u> Transformable and can have smooth shading using vertex normal interpolation<br>
  <img src="src/examples/07_Meshes.gif" alt="07_Meshes"><br>
  <p float="left">
    <img src="src/examples/07_Meshes_setup.png" alt="07_Meshes_setup" width="380">
    <img src="src/examples/07_Meshes_init.png" alt="07_Meshes_init" width="360">
  </p>
  Mesh files are in a format native to the renderer which is optimized for ray/triangle intersection.<br>
* <b><u>Textures</b>:</u> Albedo, Normal<br>
  <img src="src/examples/09_Textures.gif" alt="09_Textures"><br>
  <img src="src/examples/09_Textures.png" alt="09_Textures" width="540"><br>
  Textures can be loaded from files for use as albedo or normal maps.<br>
  Texture files are in a format native to the renderer and optimized for filtered sampling.<br> 
* <b><u>Render Modes</b>:</u> Beauty, Depth, Normals, UVs and BVHs<br>
  <img src="src/examples/08_Modes.gif" alt="08_Modes"><br>
  <p float="left">
    <img src="src/examples/07_Modes_update.png" alt="08_Modes_update" width="380">
    <img src="src/examples/07_Modes_setup.png" alt="08_Modes_setup" width="360">
  </p>
  BVHs can be shown as a wireframe overlay in any render mode.<br>
  <img src="src/examples/07_Meshes_BVH.gif" alt="08_Modes_BVH"><br>
  The BVH of the scene updates dynamically as primitives are transformed.<br>
  The BVH of meshes are only built once when a mesh file is first created.<br>
  Mesh primitives can be transformed dynamically because tracing is done in the local space of each primitive.<br>

Converting `.bmp` files to the native `.texture` files can be done with a provided CLI tool:<br>
`./bmp2texture src.bmp trg.texture [-m] [-w]`<br>
-m : Generate mip-maps<br>
-w : Wrap-around<br>

Converting `.obj` files to the native `.mesh` files can be done with a provided CLI tool:<br>
`./obj2mesh src.obj trg.mesh [-i]`<br>
-i : Invert triangle winding order (CW to CCW)<br>
Note: <b>SlimTracin</b>'s `.mesh` files are not the same as <b>SlimEngine</b>'s ones.<br>

<b>SlimTracin</b> does not come with any GUI functionality at this point.<br>
Some example apps have an optional HUD (heads up display) that shows additional information.<br>
It can be toggled on or off using the`tab` key.<br>

All examples are interactive using <b>SlimTracing</b>'s facilities having 2 interaction modes:
1. FPS navigation (WASD + mouse look + zooming)<br>
2. DCC application (default)<br>

Double clicking the `left mouse button` anywhere within the window toggles between these 2 modes.<btr>

Entering FPS mode captures the mouse movement for the window and hides the cursor.<br>
Navigation is then as in a typical first-person game (plus lateral movement and zooming):<br>

Move the `mouse` to freely look around (even if the cursor would leave the window border)<br>
Scroll the `mouse wheel` to zoom in and out (changes the field of view of the perspective)<br>
Hold `W` to move forward<br>
Hold `S` to move backward<br>
Hold `A` to move left<br>
Hold `D` to move right<br>
Hold `R` to move up<br>
Hold `F` to move down<br>

Exit this mode by double clicking the `left mouse button`.

The default interaction mode is similar to a typical DCC application (i.e: Maya):<br>
The mouse is not captured to the window and the cursor is visible.<br>
Holding the `right mouse button` and dragging the mouse orbits the camera around a target.<br>
Holding the `middle mouse button` and dragging the mouse pans the camera (left, right, up and down).<br>
Scrolling the `mouse wheel` dollys the camera forward and backward.<br>

Clicking the `left mouse button` selects an object in the scene that is under the cursor.<br>
Holding the `left mouse button` while hovering an object and then dragging the mouse,<br>
moves the object parallel to the screen.<br>

Holding `alt` highlights the currently selecte object by drawing a bounding box around it.<br>
While `alt` is still held, if the cursor hovers the selected object's bounding box,<br>
mouse interaction transforms the object along the plane of the bounding box that the cursor hovers on:<br>
Holding the `left mouse button` and dragging the mouse moves the object.<br>
Holding the `right mouse button` and dragging the mouse rotates the object.<br>
Holding the `middle mouse button` and dragging the mouse scales the object.<br>
<i>(`mouse wheel` interaction is disabled while `alt` is held)</i><br>

In some examples, further interaction is enabled while holding `ctrl` or `shift` <br>
using the `mouse wheel` as a virtual "slider":<br>
Holding `shift` and scrolling the `mouse wheel` cycles the assigned material for the selected object.<br>
Holding `ctrl` and scrolling the `mouse wheel` increases or decreases the trace-height*<br>
<i>(how many times rays are allowed to bounce around between reflective or refractive objects)</i><br>