"""Fixed MAT-9 Special transport inputs; shared by capture and identity checks.

Closed-solid Cycles transport is a comparison target, not an assertion that the
real-time single-interface/diffusion approximation implements the same transport.
Alpha blending is a separate unsupported Scene queue and is never captured as opaque.
"""
from thin_film_cases import CONTROLS


def definitions():
    return [
        ('special_transmission', {'Base Color': (.72, .85, .95, 1), 'Roughness': .12,
                                  'IOR': 1.45, 'Transmission Weight': .9}),
        ('special_glass', {'Base Color': (1, 1, 1, 1), 'Roughness': .12,
                           'IOR': 1.45, 'Transmission Weight': 1}),
        ('special_ior_one', {'Base Color': (1, 1, 1, 1), 'Roughness': 0,
                             'IOR': 1, 'Transmission Weight': 1}),
        ('special_subsurface', {'Base Color': (.86, .28, .22, 1), 'Roughness': .5,
                                'Subsurface Weight': .8, 'Subsurface Scale': .18}),
        ('special_subsurface_local', {'Base Color': (.86, .28, .22, 1), 'Roughness': .5,
                                      'Subsurface Weight': .8, 'Subsurface Scale': 0}),
        ('special_subsurface_off', {'Base Color': (.86, .28, .22, 1), 'Roughness': .5}),
        ('special_mixed', {'Base Color': (.65, .35, .2, 1), 'Roughness': .4,
                           'Metallic': .4, 'Subsurface Weight': .7, 'Subsurface Scale': .1,
                           'Coat Weight': .4, 'Coat Roughness': .2}),
        ('special_volume_emission', {'Volume Only': 1, 'Volume.Color': (0, 0, 0, 1),
                                     'Volume.Density': 0, 'Volume.Emission Color': (.7, .4, .2, 1),
                                     'Volume.Emission Strength': 2}),
        ('special_volume_absorption', {'Volume Only': 1, 'Volume.Color': (0, 0, 0, 1),
                                       'Volume.Density': .8, 'Volume.Absorption Color': (0, 0, 0, 1),
                                       'Volume.Emission Color': (.7, .4, .2, 1),
                                       'Volume.Emission Strength': 2}),
        ('special_volume_scattering', {'Volume Only': 1, 'Volume.Color': (.5, .5, .5, 1),
                                       'Volume.Density': .18, 'Volume.Anisotropy': 0}),
    ] + CONTROLS


LEGACY_BOUNCES = {'max': 8, 'diffuse': 1, 'glossy': 1, 'transmission': 8, 'transparent': 16, 'volume': 1}
BOUNCES = {**LEGACY_BOUNCES, 'volume': 0}
LEGACY_TRANSPORT = {
    'geometry': 'shared closed smooth 80-triangle icosphere, radius 0.5; within native 128-triangle volume budget',
    'cycles': 'closed-solid refraction and random-walk subsurface; single volume scattering',
    'native': 'single-interface screen/environment refraction, screen-space diffusion, homogeneous single scattering',
    'alpha': 'opaque only; Scene blended queue unsupported and separately rejected',
    'volume': 'Volume Only=1 disconnects Surface; Volume.* addresses Principled Volume inputs',
}
TRANSPORT = {**LEGACY_TRANSPORT,
             'cycles': 'closed-solid refraction and random-walk subsurface; RNA volume_bounces=0 for direct single scattering',
             'volume_light_visibility': 'self-extinction enabled; no external occluders'}


def volume_definitions():
    return [case for case in definitions() if case[0].startswith('special_volume_') or case[0]=='control_emission']


def surface_definitions():
    return [case for case in definitions() if not case[0].startswith('special_volume_')]
