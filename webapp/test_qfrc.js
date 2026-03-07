import factory from "mujoco-js/dist/mujoco_wasm.js";

async function test() {
    const mujoco = await factory();
    const modelXML = `
<mujoco model="SphereBot">
  <worldbody>
    <geom type="plane" size="10 10 0.1" rgba=".2 .2 .2 1"/>
    <body name="robot" pos="0 0 0.5">
      <freejoint name="root"/>
      <geom type="sphere" size="0.2" rgba="0 0.8 1 1"/>
    </body>
  </worldbody>
</mujoco>`;
    mujoco.FS.writeFile('/working_model.xml', modelXML);
    const model = mujoco.MjModel.loadFromXML('/working_model.xml');
    const state = new mujoco.MjData(model);
    console.log("qfrc_applied exists:", state.qfrc_applied !== undefined);
    if (state.qfrc_applied) console.log("qfrc_applied length:", state.qfrc_applied.length);

    state.qfrc_applied[0] = 500;
    mujoco.mj_step(model, state);
    console.log("pos after step:", state.qpos[0]);

    for (let i = 0; i < 100; i++) {
        state.qfrc_applied[0] = 5000;
        mujoco.mj_step(model, state);
    }
    console.log("pos after 100 steps:", state.qpos[0]);
}
test();
