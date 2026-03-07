import loadMujoco from 'mujoco-js';

const sphereModel = `
<mujoco model="SphereBot">
  <worldbody>
    <light pos="0 0 3"/>
    <geom type="plane" size="10 10 0.1" rgba=".2 .2 .2 1"/>
    <body name="robot" pos="0 0 0.5">
      <freejoint name="root"/>
      <geom type="sphere" size="0.2" rgba="0 0.8 1 1"/>
    </body>
  </worldbody>
</mujoco>`;

async function init() {
    console.log("Loading..");
    const mujoco = await loadMujoco();
    const FS = mujoco.FS;
    FS.writeFile('/test.xml', sphereModel);
    console.log("trying loadFromXML with path");
    try {
        let m1 = mujoco.MjModel.loadFromXML('/test.xml');
        console.log("Success with path! model:", m1 !== null);
    } catch (e) {
        console.log("Failed with path", e);
    }
}
init();
