"""Fixed MAT-9 film sweep, declared before the first native capture."""
VARIANTS = (
    ('subnanometer', .25, 1.4, .6, .5, 0.),
    ('dielectric_100', 100., 1.5, 0., .5, 0.),
    ('dielectric_1000', 1000., 2., 0., .5, 0.),
    ('metal_100', 100., 1.5, 1., .5, 0.),
    ('metal_1000', 1000., 2., 1., .5, 0.),
    ('high_ior', 200., 3., .6, .5, 0.),
    ('rough', 550., 1.4, .6, .88, 0.),
    ('coated', 550., 1.4, .6, .5, .7),
)
CONTROLS = [('control_emission', {'Base Color': (0, 0, 0, 1), 'Specular IOR Level': 0,
             'Emission Color': (.3, .55, 1, 1), 'Emission Strength': 5}),
            ('control_white', {'Base Color': (1, 1, 1, 1), 'IOR': 1, 'Roughness': 1})]


def definitions():
    result = []
    for name, thickness, ior, metal, rough, coat in VARIANTS:
        common = {'Base Color': (.32, .42, .7, 1.), 'Metallic': metal,
                  'Roughness': rough, 'Coat Weight': coat, 'Thin Film IOR': ior}
        result.extend((('film_' + name, {**common, 'Thin Film Thickness': thickness}),
                       ('off_' + name, {**common, 'Thin Film Thickness': 0.})))
    return result + CONTROLS
