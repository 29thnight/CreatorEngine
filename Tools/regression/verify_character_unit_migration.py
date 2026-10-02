import importlib.util
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location("migration", Path(__file__).with_name("report-character-unit-migration.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
checks = 0

def check(value):
    global checks
    assert value
    checks += 1

fields = dict(m_radius=.55, m_height=2, maxSpeed=1.025, acceleration=1, staticFriction=.4,
              dynamicFriction=.1, jumpSpeed=.05, gravityWeight=.2, m_fBaseSpeed=.025, m_fFinalMultiplierSpeed=1)
report = module.convert(fields, 1/60)
values = report["convertedMovement"]
check(math.isclose(values["serializedMaxSpeedMetresPerSecond"], 61.5))
check(math.isclose(values["steadyMaxSpeedMetresPerSecond"], 1.5))
check(math.isclose(values["accelerationMetresPerSecondSquared"], 60))
check(math.isclose(values["gravityMetresPerSecondSquared"], -12))
check(math.isclose(values["jumpVelocityMetresPerSecond"], 3))
check(report["authoringProposal"]["m_cylinderHeight"] == 2)
check(report["authoringProposal"]["m_stepOffset"] == .001)
# Converted damping preserves old lerp retention exactly at the declared baseline step.
check(math.isclose(math.exp(-values["staticDamping"]["decayPerSecond"] / 60), .6))
check(math.isclose(module.convert(fields, 1/30)["convertedMovement"]["gravityMetresPerSecondSquared"], -6))
check(module.convert(dict(fields, staticFriction=1), 1/60)["convertedMovement"]["staticDamping"]["instantStop"])
for dt in (0, -1, float("nan"), float("inf"), 1e-20, 2):
    try: module.convert(fields, dt)
    except ValueError: check(True)
    else: raise AssertionError("invalid baseline accepted")
for key, value in (("gravityWeight", float("nan")), ("m_radius", 0), ("dynamicFriction", 1.1),
                   ("maxSpeed", 1e308), ("m_fFinalMultiplierSpeed", True)):
    try: module.convert(dict(fields, **{key: value}), 1/60)
    except ValueError: check(True)
    else: raise AssertionError("invalid field accepted")
check(module.collect({"entities": [{"CharacterControllerComponent": 1, **fields}]}, 1/60)[0]["sourcePath"] == "$.entities[0]")
print(f"CHARACTER_UNIT_MIGRATION_OK checks={checks}")
