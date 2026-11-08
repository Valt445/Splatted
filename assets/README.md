# assets/

Splat scenes go here as `.glb` (glTF with the `KHR_gaussian_splatting` extension).

Convert a `.ply` scene (e.g. the pre-trained ones from the official 3D Gaussian Splatting repo):

    npx @playcanvas/splat-transform scene.ply scene.glb

Or use SuperSplat Convert in the browser. Start with a small scene.

Reminder vs PLY: in glTF, scale is already linear, opacity is already post-sigmoid,
the quaternion is stored xyzw (w LAST), and +Y is up.
