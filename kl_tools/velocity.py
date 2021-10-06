import numpy as np
import os
from abc import ABC, abstractmethod
import matplotlib.pyplot as plt
from argparse import ArgumentParser
import astropy.constants as const
import astropy.units as units

# i don't understand how to make this work correctly...
# from . import utils
# from . import transformation as transform
# from . import numba_transformation as numba_transform
import utils
from transformation import TransformableImage
import transformation as transform
import numba_transformation as numba_transform
from parameters import pars2theta

import pudb

parser = ArgumentParser()

parser.add_argument('--show', action='store_true', default=False,
                    help='Set to show test plots')
parser.add_argument('--test', action='store_true', default=False,
                    help='Set to run tests')

class VelocityModel(object):
    '''
    Default velocity model. Can subclass to
    define your own model
    '''

    name = 'centered'
    _model_params = ['v0', 'vcirc', 'rscale', 'sini',
                     'theta_int', 'g1', 'g2', 'r_unit', 'v_unit']

    def __init__(self, model_pars):
        if not isinstance(model_pars, dict):
            t = type(model_pars)
            raise TypeError(f'model_pars must be a dict, not a {t}!')

        self.pars = model_pars

        self._check_model_pars()

        # Needed if using numba for transformations
        self._build_pars_array()

        return

    def _check_model_pars(self):
        mname = self.name

        # Make sure there are no undefined pars
        for name, val in self.pars.items():
            if val is None:
                raise ValueError(f'{param} must be set!')
            if name not in self._model_params:
                raise AttributeError(f'{name} is not a valid model ' +\
                                     f'parameter for {mname} velocity model!')

        # Make sure all req pars are present
        for par in self._model_params:
            if par not in self.pars:
                raise AttributeError(f'{par} must be in passed parameters ' +\
                                     f'to instantiate {mname} velocity model!')

        # Make sure units are astropy.unit objects
        for u in [self.pars['r_unit'], self.pars['v_unit']]:
            if (not isinstance(u, units.Unit)) and \
               (not isinstance(u, units.CompositeUnit)) and \
               (not isinstance(u, units.IrreducibleUnit)):
                raise TypeError('unit params must be an astropy unit class!')

        return

    def _build_pars_array(self):
        '''
        Numba requires a pars array instead of a more flexible dict
        '''

        self.pars_arr = pars2theta(self.pars)

        return

    def get_transform_pars(self):
        pars = {}
        for name in ['g1', 'g2', 'sini', 'theta_int']:
            pars[name] = self.pars[name]

        return pars

class VelocityMap(TransformableImage):
    '''
    Base class for a velocity map

    It is helpful to compute various things in different coordinate
    planes. The available ones are defined as follows:

    disk: Face-on view of the galactic disk, no inclination angle.
          This will be cylindrically symmetric for most models

    gal:  Galaxy major/minor axis frame with inclination angle same as
          source plane. Will now be ~ellipsoidal for sini!=0

    source: View from the lensing source plane, rotated version of gal
            plane with theta = theta_intrinsic

    obs:  Observed image plane. Sheared version of source plane
    '''

    def __init__(self, model_name, model_pars):
        '''
        model_name: The name of the velocity to model.
        model_pars: A dict with key:val pairs for the model parameters.
                    Must be a registered velocity model.
        '''


        self.model_name = model_name
        self.model = build_model(model_name, model_pars)

        transform_pars = self.model.get_transform_pars()
        super(VelocityMap, self).__init__(transform_pars)

        return

    def __call__(self, plane, x, y, speed=False, normalized=False,
                 use_numba=False):
        '''
        Evaluate the velocity map at position (x,y) in the given plane. Note
        that position must be defined in the same plane

        speed: bool
            Set to True to return speed map instead of velocity
        normalized: bool
            Set to True to return velocity / c
        use_numba: bool
            Set to True to use numba versions of transformations
        '''

        super(VelocityMap, self).__call__(
            plane, x, y, use_numba=use_numba
            )

        if normalized is True:
            norm = 1. / const.c.to(self.model.pars['v_unit']).value
        else:
            norm = 1.

        return norm * self._eval_map_in_plane(
            plane, x, y, speed=speed, use_numba=use_numba
            )

    def _eval_map_in_plane(self, plane, x, y, speed=False, use_numba=False):
        '''
        We use static methods defined in transformation.py
        to speed up these very common function calls

        The input (x,y) position is defined in the plane

        # The input (x,y) position is transformed from the obs
        # plane to the relevant plane

        speed: bool
            Set to True to return speed map instead of velocity
        use_numba: bool
            Set to True to use numba versions of transformations
        '''

        # Need to use array for numba
        if use_numba is True:
            pars = self.model.pars_arr
        else:
            pars = self.model.pars

        func = self._get_plane_eval_func(plane, use_numba=use_numba)

        return func(pars, x, y, speed=speed)

    @classmethod
    def _eval_in_gal_plane(cls, pars, x, y, **kwargs):
        '''
        pars: dict
            Holds the model & transformation parameters
        x,y: np.ndarray
            The position coordintates in the gal plane

        kwargs holds any additional params that might be needed
        in subclass evaluations, such as using speed instead of
        velocity
        '''

        xp, yp = transform._gal2disk(pars, x, y)

        # Need the speed map in either case
        speed = kwargs['speed']
        kwargs['speed'] = True
        speed_map = cls._eval_in_disk_plane(pars, xp, yp, **kwargs)

        # speed will be in kwargs
        if speed is True:
            return speed_map

        else:
            # euler angles which handle the vector aspect of velocity transform
            sini = pars['sini']
            phi = np.arctan2(yp, xp)

            return sini * np.cos(phi) * speed_map

    @classmethod
    def _eval_in_disk_plane(cls, pars, x, y, **kwargs):
        '''
        Evaluates model at posiiton array in the disk plane, where
        pos=(x,y) is defined relative to galaxy center

        pars is a dict with model parameters

        will eval speed map instead of velocity if speed is True
        '''

        if kwargs['speed'] is False:
            return np.zeros(np.shape(x))

        r = np.sqrt(x**2 + y**2)

        atan_r = np.arctan(r  / pars['rscale'])

        v_r = pars['v0'] + (2./ np.pi) * pars['vcirc'] * atan_r

        return v_r

    def plot(self, plane, x=None, y=None, rmax=None, show=True, close=True,
             title=None, size=(9,8), center=True, outfile=None, speed=False,
             normalized=False):
        '''
        Plot speed or velocity map in given plane. Will create a (x,y)
        grid based off of rmax centered at 0 in the chosen plane unless
        (x,y) are passed

        Can normalize to v/c if desired
        '''

        if plane not in self._planes:
            raise ValueError(f'{plane} not a valid image plane to plot!')

        pars = self.model.pars

        if rmax is None:
            rmax = 5. * pars['rscale']

        if (x is None) or (y is None):
            if (x is not None) or (y is not None):
                raise ValueError('Can only pass both (x,y), not just one')

            # make square position grid in given plane
            Nr = 100
            dr = rmax / (Nr+1)
            r = np.arange(0, rmax+dr, dr)

            dx = (2*rmax) / (2*Nr+1)
            xarr = np.arange(-rmax, rmax+dx, dx)

            x, y = np.meshgrid(xarr, xarr)

        else:
            # parse given positions
            assert type(x) == type(y)
            if not isinstance(x, np.ndarray):
                x = np.array(x)
                y = np.array(y)

            if x.shape != y.shape:
                raise ValueError('x and y must have the same shape!')

        V = self(plane, x, y, speed=speed, normalized=normalized)

        runit = pars['r_unit']
        vunit = pars['v_unit']

        if normalized is True:
            cstr = ' / c'
        else:
            cstr = ''

        if speed is True:
            mtype = 'Speed'
        else:
            mtype = 'Velocity'

        plt.pcolormesh(x, y, V)
        plt.colorbar(label=f'{vunit}{cstr}')

        runit = pars['r_unit']
        plt.xlabel(f'{runit}')
        plt.ylabel(f'{runit}')

        if center is True:
            plt.plot(0, 0, 'r', ms=10, markeredgewidth=2, marker='x')

        if title is not None:
            plt.title(title)
        else:
            rscale = pars['rscale']
            plt.title(f'{mtype} map in {plane} plane\n' +\
                      f'r_0 = {rscale} {runit}')

        if size is not None:
            plt.gcf().set_size_inches(size)

        if outfile is not None:
            plt.savefig(outfile, bbox_inches='tight', dpi=300)

        if show is True:
            plt.show()

        if close is True:
            plt.close()

        return

    def plot_all_planes(self, plot_kwargs={}, show=True, close=True, outfile=None,
                        size=(9, 8), speed=False, normalized=False, center=True):
        if size not in plot_kwargs:
            plot_kwargs['size'] = size

        # Overwrite defaults if present
        if show in plot_kwargs:
            show = plot_kwargs['show']
            del plot_kwargs['show']
        if close in plot_kwargs:
            close = plot_kwargs['close']
            del plot_kwargs['close']
        if outfile in plot_kwargs:
            outfile = plot_kwargs['outfile']
            del plot_kwargs['outfile']

        pars = self.model.pars

        if 'rmax' in plot_kwargs:
            rmax = plot_kwargs['rmax']
        else:
            rmax = 5. * pars['rscale']
            plot_kwargs['rmax'] = rmax

        # Create square grid centered at source for all planes
        Nr = 100
        dr = rmax / (Nr+1)
        r = np.arange(0, rmax+dr, dr)

        dx = (2*rmax) / (2*Nr+1)
        x = np.arange(-rmax, rmax+dx, dx)

        X, Y = np.meshgrid(x,x)

        # Figure out subplot grid
        Nplanes = len(self._planes)
        sqrt = np.sqrt(Nplanes)

        if sqrt % 1 == 0:
            N = sqrt
        else:
            N = int(sqrt+1)

        k = 1
        for plane in self._planes:
            plt.subplot(N,N,k)
            # X, Y = transform.transform_coords(Xdisk, Ydisk, 'disk', plane, pars)
            # X,Y = Xdisk, Ydisk
            # Xplane, Yplane = transform.transform_coords(X,Y, plane)
            # X,Y = transform.transform_coords(Xdisk, Ydisk, 'disk', 'obs', pars)
            self.plot(plane, x=X, y=Y, show=False, close=False, speed=speed,
                      normalized=normalized, center=center, **plot_kwargs)
            k += 1

        plt.tight_layout()

        if outfile is not None:
            plt.savefig(outfile, bbox_inches='tight', dpi=300)

        if show is True:
            plt.show()

        if close is True:
            plt.close()

        return

    def plot_map_transforms(self, size=(9,8), outfile=None, show=True, close=True,
                            speed=False, center=True):

        pars = self.model.pars

        runit = pars['r_unit']
        vunit = pars['v_unit']
        rscale = pars['rscale']

        rmax = 5. * rscale
        Nr = 100

        Nx = 2*Nr + 1
        dx = 2*rmax / Nx
        x = np.arange(-rmax, rmax+dx, dx)
        y = np.arange(-rmax, rmax+dx, dx)

        # uniform grid in disk plane
        # Xdisk, Ydisk= np.meshgrid(x, y)
        X, Y = np.meshgrid(x, y)

        xlim, ylim = [-rmax, rmax], [-rmax, rmax]

        if speed is True:
            mtype = 'Speed'
        else:
            mtype = 'Velocity'

        planes = ['disk', 'gal', 'source', 'obs']
        for k, plane in enumerate(planes):
            Xplane, Yplane = transform.transform_coords(X, Y, 'disk', plane, pars)
            Vplane = self(plane, Xplane, Yplane, speed=speed)

            plt.subplot(2,2,k+1)
            plt.pcolormesh(Xplane, Yplane, Vplane)
            plt.colorbar(label=f'{vunit}')
            plt.xlim(xlim)
            plt.ylim(ylim)
            plt.xlabel(f'{runit}')
            plt.ylabel(f'{runit}')
            plt.title(f'{mtype} map for {plane} plane\n' +\
                      f'r_scale = {rscale} {runit}')

            if center is True:
                plt.plot(0, 0, 'r', ms=10, markeredgewidth=2, marker='x')

        plt.gcf().set_size_inches(size)
        plt.tight_layout()

        if outfile is not None:
            plt.savefig(outfile, bbox_inches='tight', dpi=300)

        if show is True:
            plt.show()

        if close is True:
            plt.close()

        return

def get_model_types():
    return MODEL_TYPES

# NOTE: This is where you must register a new model
MODEL_TYPES = {
    'default': VelocityModel,
    'centered': VelocityModel,
    }

def build_model(name, pars, logger=None):
    name = name.lower()

    if name in MODEL_TYPES.keys():
        # User-defined input construction
        model = MODEL_TYPES[name](pars)
    else:
        raise ValueError(f'{name} is not a registered velocity model!')

    return model

def main(args):
    '''
    For now, just used for testing the classes
    '''

    show = args.show

    center = False

    model_name = 'centered'
    model_pars = {
        'v0': 0, # km/s
        'vcirc': 200, # km/s
        # 'r0': 0, # kpc
        'rscale': 3, #kpc
        # 'rscale': 128./5, # pixels
        'sini': 0.8,
        'theta_int': np.pi/3, # rad
        # 'r_unit': units.kpc,
        'r_unit': units.Unit('pixel'),
        'v_unit': units.km / units.s,
        'g1': 0.1,
        'g2': 0.05,
        # 'g1': 0,
        # 'g2': 0
    }

    print('Creating VelocityMap from params')
    vmap = VelocityMap(model_name, model_pars)

    print('Making speed map transform plots')
    outfile = os.path.join(outdir, f'speedmap-transorms.png')
    vmap.plot_map_transforms(
        outfile=outfile, show=show, speed=True, center=center
        )

    print('Making velocity map transform plots')
    outfile = os.path.join(outdir, f'vmap-transorms.png')
    vmap.plot_map_transforms(
        outfile=outfile, show=show, speed=False, center=center)

    # print('Making alt speed map transform plots')
    # outfile = os.path.join(outdir, f'alt-speedmap-transorms.png')
    # vmap.plot_map_transforms_alt(outfile=outfile, show=show, speed=True)

    for plane in ['disk', 'gal', 'source', 'obs']:
        print(f'Making plot of velocity field in {plane} plane')
        outfile = os.path.join(outdir, f'vmap-{plane}.png')
        vmap.plot(
            plane, outfile=outfile, show=False, center=center
            )

    print('Making combined velocity field plot')
    plot_kwargs = {}
    outfile = os.path.join(outdir, 'vmap-all.png')
    vmap.plot_all_planes(
        plot_kwargs=plot_kwargs, show=show, outfile=outfile, center=center
        )

    print('Making combined velocity field plot normalized by c')
    plot_kwargs = {}
    outfile = os.path.join(outdir, 'vmap-all-normed.png')
    vmap.plot_all_planes(
        plot_kwargs=plot_kwargs, show=show, outfile=outfile,
                         normalized=True, center=center
        )

    return 0

if __name__ == '__main__':
    args = parser.parse_args()

    if args.test is True:
        outdir = os.path.join(utils.TEST_DIR, 'velocity')
        utils.make_dir(outdir)

        print('Starting tests')
        rc = main(args)

        if rc == 0:
            print('All tests ran succesfully')
        else:
            print(f'Tests failed with return code of {rc}')
