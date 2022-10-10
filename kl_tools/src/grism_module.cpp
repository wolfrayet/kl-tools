#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <map>
#include <cmath>
#include <cstdlib>
#include <string>
#include <complex>
#include <vector>
#include <time.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <omp.h>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)
#define _DEBUG_PRINTS_ 0

namespace py = pybind11;

#include "galsim/GSParams.h"
#include "galsim/SBInclinedExponential.h"
#include "galsim/Image.h"
#include "galsim/Random.h"
#define PI (3.14159265359)
typedef py::array_t<double, py::array::c_style | py::array::forcecast> ndarray;
using namespace std;

int _MPI_SIZE = -1;
int _MPI_RANK = -1;
/** test how global variable works when called by Python MPI **/
struct parcel{
    int x;
    int y;
    int z;
    double val;
};
parcel global_parcel{.x=1,.y=2,.z=3,.val=114.514};
void print_parcel(){
    cout << "GP = " << global_parcel.x << ", " << global_parcel.y << ", " <<\
    global_parcel.z << ", " << global_parcel.val << endl;
}
void set_parcel(int i, int j, int k, double val){
    global_parcel.x = i;
    global_parcel.y = j;
    global_parcel.z = k;
    global_parcel.val = val;
    print_parcel();
}


/** end testing python MPI **/

/** wrapper function interface **/
/*
void cpp_add_disperse_helper(const py::dict &config,
                             const ndarray lambdas,
                             const ndarray bandpasses);
void cpp_get_dispersed_image(int index, const ndarray theory_data,
                             ndarray dispersed_data);
*/
void cpp_add_grism_observation(const py::dict &config,
                         const ndarray lambdas,
                         const ndarray bandpasses,
                         const ndarray data, const ndarray noise);
void cpp_add_image_observation(const py::dict &config,
                               const ndarray data, const ndarray noise);
void cpp_get_dispersed_image(int index, const ndarray theory_data,
                             ndarray dispersed_data);
double cpp_get_chi2(int index, const ndarray modelImage);
void cpp_clear_observation();
int cpp_get_Nobs();
void cpp_set_mpi_info(int size, int rank);

namespace interface_mpp_aux {
/** internal functions and classes **/

/* Dispersion Relation
 * At the first call, the function would init the dispersion relation.
 * For a galaxy at real position (xcen,ycen), and with
 * dispersion angle theta, the wavelength lam gets dispersed
 * to the new position:
 *      x = xcen + (lam * dx/dlam + offset) * cos(theta),
 *      y = ycen + (lam * dx/dlam + offset) * sin(theta)
 * Input
 *      double lam: central wavelength in nm of the current slice
 *      vector<double> &shift: the returned resulting shift vector.
 * */

    struct pixel_response {
        int image_x; // x index of dispersed image
        int image_y; // y index of dispersed image
        int cube_z; // z (wavelength) index of theory cube
        int cube_x; // x index of theory cube
        int cube_y; // y index of theory cube
        double weight; // weight of the theory cube
    };

    class disperse_helper {

    public:
        disperse_helper(const py::dict &config,
                        const ndarray lambdas,
                        const ndarray bandpasses);

        disperse_helper(const py::dict &config);

        void set_disperse_helper(const py::dict &config,
                                 const ndarray lambdas,
                                 const ndarray bandpasses);

        void get_dispersed_image(const ndarray theory_data,
                                 ndarray dispersed_data) const;

        int getNx() const { return this->Nx; }

        int getNy() const { return this->Ny; }

        double getPixScale() const { return this->pix_scale; }

        int getModelNx() const { return this->model_Nx; }

        int getModelNy() const { return this->model_Ny; }

        int getModelNlam() const { return this->model_Nlam; }

        double getModelScale() const { return this->model_scale; }

    private:
        // configuration parameters
        int model_Nx, model_Ny, model_Nlam; // theory model cube dimension
        double model_scale; // theory model cube pixel scale
        int Nx, Ny; // observed image dimension
        double pix_scale; // observed image pixel scale
        double R_spec; // grism spectral resolution at 1 micron
        double disp_ang; // dispersion angle, radian
        double offset; // offset in units of observed pixels
        double diameter; // aperture diameter in cm
        double exp_time; // exposure time in seconds
        double gain; // detector gain
        // disperse relation table
        vector <pixel_response> pixel_response_table;

        int set_pixel_response(const ndarray lambdas, const ndarray bandpasses);

        void get_dispersion(double lam, vector<double> &shift);

        double img2cube_arcsec(double center, int edge, double shift_in_pix, double ref) {
            return center + (edge * 0.5 - shift_in_pix) * pix_scale - ref;
        }
    };

    disperse_helper::disperse_helper(const py::dict &config, const ndarray lambdas,
                                     const ndarray bandpasses) {
        model_Nx = py::int_(config["model_Nx"]);
        model_Ny = py::int_(config["model_Ny"]);
        model_Nlam = py::int_(config["model_Nlam"]);
        model_scale = py::float_(config["model_scale"]);
        Nx = py::int_(config["Nx"]);
        Ny = py::int_(config["Ny"]);
        pix_scale = py::float_(config["pix_scale"]);
        R_spec = py::float_(config["R_spec"]);
        disp_ang = py::float_(config["disp_ang"]);
        offset = py::float_(config["offset"]);
        diameter = py::float_(config["diameter"]);
        exp_time = py::float_(config["exp_time"]);
        gain = py::float_(config["gain"]);
        int status = set_pixel_response(lambdas, bandpasses);
        assert(status == 0);
    }

// this is used for photometry data, an empty placeholder
    disperse_helper::disperse_helper(const py::dict &config) {
        model_Nx = py::int_(config["model_Nx"]);
        model_Ny = py::int_(config["model_Ny"]);
        model_Nlam = py::int_(config["model_Nlam"]);
        model_scale = py::float_(config["model_scale"]);
        Nx = py::int_(config["Nx"]);
        Ny = py::int_(config["Ny"]);
        pix_scale = py::float_(config["pix_scale"]);
        diameter = py::float_(config["diameter"]);
        exp_time = py::float_(config["exp_time"]);
        gain = py::float_(config["gain"]);
    }

    void disperse_helper::set_disperse_helper(const py::dict &config,
                                              const ndarray lambdas,
                                              const ndarray bandpasses) {
        model_Nx = py::int_(config["model_Nx"]);
        model_Ny = py::int_(config["model_Ny"]);
        model_Nlam = py::int_(config["model_Nlam"]);
        model_scale = py::float_(config["model_scale"]);
        Nx = py::int_(config["Nx"]);
        Ny = py::int_(config["Ny"]);
        pix_scale = py::float_(config["pix_scale"]);
        R_spec = py::float_(config["R_spec"]);
        disp_ang = py::float_(config["disp_ang"]);
        offset = py::float_(config["offset"]);
        diameter = py::float_(config["diameter"]);
        exp_time = py::float_(config["exp_time"]);
        gain = py::float_(config["gain"]);
        // update the pixel response
        int status = set_pixel_response(lambdas, bandpasses);
        assert(status == 0);
    }

    void disperse_helper::get_dispersion(double lam, vector<double> &shift) {
        shift[0] = (lam * (R_spec / 500.0) + offset) * cos(disp_ang);
        shift[1] = (lam * (R_spec / 500.0) + offset) * sin(disp_ang);
    }

    int disperse_helper::set_pixel_response(const ndarray lambdas,
                                            const ndarray bandpasses) {
        int i, j, k;
        double l, r, t, b, lb, rb, tb, bb;
        int li, ri, ti, bi;
        // sanity check on lambdas array
        py::buffer_info buf_l = lambdas.request();
        py::buffer_info buf_bp = bandpasses.request();
        if (buf_l.ndim != 2)
            throw runtime_error("`lambdas` dimension must be 2!");
        if (buf_bp.ndim != 2)
            throw runtime_error("`bandpasses` dimension must be 2!");
        if ((buf_l.shape[0] != model_Nlam) || (buf_bp.shape[0] != model_Nlam))
            throw runtime_error("`lambdas` has wrong Nlam!");
        auto *ptr_l = static_cast<double *>(buf_l.ptr);
        auto *ptr_bp = static_cast<double *>(buf_bp.ptr);

        /*************** start pixel response calculation ******************/
        // init coordinates
        // theory model cube
        double Rx_theory = (int) (model_Nx / 2) - 0.5 * ((model_Nx - 1) % 2);
        double Ry_theory = (int) (model_Ny / 2) - 0.5 * ((model_Ny - 1) % 2);
        vector<double> origin_Xgrid(model_Nx, 0.0);
        vector<double> origin_Ygrid(model_Ny, 0.0);
        for (i = 0; i < model_Nx; i++) { origin_Xgrid[i] = (i - Rx_theory) * model_scale; }
        for (i = 0; i < model_Ny; i++) { origin_Ygrid[i] = (i - Ry_theory) * model_scale; }
        double ob_x = origin_Xgrid[0] - 0.5 * model_scale;
        double ob_y = origin_Ygrid[0] - 0.5 * model_scale;
        // observed image
        double Rx = (int) (Nx / 2) - 0.5 * ((Nx - 1) % 2);
        double Ry = (int) (Ny / 2) - 0.5 * ((Ny - 1) % 2);
        vector<double> target_Xgrid(Nx, 0.0);
        vector<double> target_Ygrid(Ny, 0.0);
        for (i = 0; i < Nx; i++) { target_Xgrid[i] = (i - Rx) * pix_scale; }
        for (i = 0; i < Ny; i++) { target_Ygrid[i] = (i - Ry) * pix_scale; }
        if (_DEBUG_PRINTS_) {
            cout << "Rx_theory = " << Rx_theory << endl;
            cout << "Init X grid (theory cube): " << endl;
            for (auto item: origin_Xgrid)
                cout << item << " " << endl;
            cout << endl << "Ry_theory = " << Ry_theory << endl;
            cout << "Init Y grid (theory cube): " << endl;
            for (auto item: origin_Ygrid)
                cout << item << " " << endl;
            cout << endl;
            cout << "corner of the theory cube frame: " << ob_x << ob_y << endl;
            cout << "Rs_obs = " << Rx << endl;
            cout << "Init X grid (observed image): " << endl;
            for (auto item: target_Xgrid)
                cout << item << " " << endl;
            cout << endl << "Ry_obs = " << Ry << endl;
            cout << endl << "Init Y grid (observed image): " << endl;
            for (auto item: target_Ygrid)
                cout << item << " " << endl;
            cout << endl;
        }
        // exptime calculation
        double flux_scale = PI * pow((diameter / 2.0), 2) * exp_time / gain;
        // looping through theory data cube
        cout << "[" << _MPI_RANK << "/" << _MPI_SIZE << "] ";
        cout << "Setting pixel response table" << endl;
        cout << "[" << _MPI_RANK << "/" << _MPI_SIZE << "] ";
        cout << "Theory model cube:" << endl;
        cout << "\tscale = " << model_scale;
        cout << "\tdimension = (" << model_Nlam << ", " << model_Ny << ", " << model_Nx << ")" << endl;
        cout << "[" << _MPI_RANK << "/" << _MPI_SIZE << "] ";
        cout << "Dispersed image dimension:" << endl;
        cout << "\tscale = " << pix_scale;
        cout << "\tdimension = (" << Ny << ", " << Nx << ")" << endl;
        if (pixel_response_table.size() > 0) { pixel_response_table.clear(); }
        for (i = 0; i < model_Nlam; i++) {
            vector<double> shift{0.0, 0.0}; // in units of pixel
            double blue_limit = ptr_l[2 * i + 0];
            double red_limit = ptr_l[2 * i + 1];
            double mean_wave = (blue_limit + red_limit) / 2.;
            // take the linear average of the bandpass.
            // Note that this only works when the lambda grid is fine enough.
            double mean_bp = (ptr_bp[2 * i + 0] + ptr_bp[2 * i + 1]) / 2.0;
            // for each slice, disperse & interpolate
            get_dispersion(mean_wave, shift);
            if (_DEBUG_PRINTS_) {
                cout << "slice " << i << " shift = (" << shift[0] << \
            ", " << shift[1] << ")" << "mean wavelength = " \
 << mean_wave << endl;
            }

            // loop through the dispersed image
            for (j = 0; j < Ny; j++) {
                for (k = 0; k < Nx; k++) {
                    // For each pixel in the dispersed image, find its original
                    // pixels who contribute its flux. Then distribute the photons
                    // from the theory cube to the observed image. If part of the
                    // cell is involved, linear interpolation is applied.
                    // For dispersed pixel (j,k), find its corners position in
                    // arcsec, then map these corners to theory model cube, in units
                    // of arcsec w.r.t. the lower-left corner of the theory model
                    // cube pixel.

                    l = img2cube_arcsec(target_Xgrid[k], -1, shift[0], ob_x);
                    r = img2cube_arcsec(target_Xgrid[k], 1, shift[0], ob_x);
                    b = img2cube_arcsec(target_Ygrid[j], -1, shift[1], ob_y);
                    t = img2cube_arcsec(target_Ygrid[j], 1, shift[1], ob_y);
                    lb = fmin(fmax(l / model_scale, 0), model_Nx);
                    rb = fmin(fmax(r / model_scale, 0), model_Nx);
                    bb = fmin(fmax(b / model_scale, 0), model_Ny);
                    tb = fmin(fmax(t / model_scale, 0), model_Ny);
                    li = floor(lb);
                    ri = ceil(rb);
                    bi = floor(bb);
                    ti = ceil(tb);
                    // begin distribution
                    if ((li == ri) || (bi == ti)) { continue; }//pixel outside the range
                    else {
                        int _nx = ri - li;
                        int _ny = ti - bi;
                        vector<double> x_weight(_nx, 1.0);
                        vector<double> y_weight(_ny, 1.0);
                        if (_nx > 1) {
                            x_weight[0] = 1.0 + li - lb;
                            x_weight[_nx - 1] = 1.0 + rb - ri;
                        } else { x_weight[0] = rb - lb; }

                        if (_ny > 1) {
                            y_weight[0] = 1.0 + bi - bb;
                            y_weight[_ny - 1] = 1.0 + tb - ti;
                        } else { y_weight[0] = tb - bb; }
                        // linear interpolation
                        for (int p = 0; p < _ny; p++) {
                            for (int q = 0; q < _nx; q++) {
                                int _k = p + bi;
                                int _l = q + li;
                                // record the response here
                                // dispersed image index: y=j, x=k
                                // theory cube index: lam=i, y=_k, x=_l
                                // weight: x_weight[q]*y_weight[p]*mean_bp*flux_scale
                                pixel_response _res;
                                _res.image_x = k;
                                _res.image_y = j;
                                _res.cube_x = _l;
                                _res.cube_y = _k;
                                _res.cube_z = i;
                                _res.weight = x_weight[q] * y_weight[p] * mean_bp * flux_scale;
                                pixel_response_table.push_back(_res);
                            }
                        }
                    }
                    // end distribution
                }// End x-loop, obs image
            }// End y-loop, obs image
        }// End lambda-loop, theory cube
        cout << "[" << _MPI_RANK << "/" << _MPI_SIZE << "] ";
        cout << "Pixel res. table size = " << pixel_response_table.size() << endl;
        return 0;
    }

    void disperse_helper::get_dispersed_image(const ndarray theory_data,
                                              ndarray dispersed_data) const {
        // sanity check
        py::buffer_info buf_td = theory_data.request();
        py::buffer_info buf_dd = dispersed_data.request();

        if (buf_td.ndim != 3)
            throw runtime_error("`theory_data` dimension must be 3!");
        if (buf_dd.ndim != 2)
            throw runtime_error("`dispersed_data` dimension must be 2!");
        if (buf_td.shape[0] != model_Nlam)
            throw runtime_error("`theory_data`, must have the same Nlam!");
        if ((buf_td.shape[1] != model_Ny) || (buf_td.shape[2] != model_Nx))
            throw runtime_error("`theory_data` dimension wrong!");
        if (buf_dd.shape[0] != Ny || buf_dd.shape[1] != Nx)
            throw runtime_error("`dispersed_data` dimension wrong!");
        // get pointer to the buffer data memory
        auto *ptr_td = static_cast<double *>(buf_td.ptr);
        auto *ptr_dd = static_cast<double *>(buf_dd.ptr);

        // init dispersed_data
        for (size_t index = 0; index < buf_dd.size; index++) { ptr_dd[index] = 0.0; }
        // begin distribution
        unsigned int thread_qty = max(atoi(getenv("OMP_NUM_THREADS")), 1);
        omp_set_num_threads(thread_qty);

#pragma omp parallel shared(dispersed_data, theory_data, ptr_dd, \
    ptr_td, pixel_response_table)
        {
            vector<double> local_copy(Ny * Nx, 0.0);
            #pragma omp for
            for (int k = 0; k < pixel_response_table.size(); k++) {
                const auto &item = pixel_response_table[k];
                // record the response here
                // dispersed image index: y=j, x=k
                // theory cube index: lam=i, y=_k, x=_l
                // weight: x_weight[q]*y_weight[p]*mean_bp*flux_scale
                size_t local_copy_id = item.image_y * Nx + item.image_x;
                size_t td_id = theory_data.index_at(item.cube_z, item.cube_y, item.cube_x);

                local_copy[local_copy_id] += ptr_td[td_id] * item.weight;
            }
            for (int j = 0; j < Ny; j++) {
                for (int i = 0; i < Nx; i++) {
                    size_t dd_id = dispersed_data.index_at(j, i);
                    #pragma omp critical
                    {
                        ptr_dd[dd_id] += local_copy[j * Nx + i];
                    }
                }
            }
        }
    }
/*
class HelperCollection{
public:
    static HelperCollection& get_instance(){
        static HelperCollection instance;
        return instance;
    }
    ~HelperCollection() = default;
    void push_back(const disperse_helper& item){
        cout << "Adding disperse_helper to the collection." << endl;
        this->helper_list.push_back(item);
        cout << this->helper_list.size() << " helper in this list" << endl;
    }
    const disperse_helper& get_helper(int index) const{
        return this->helper_list.at(index);
    }
private:
    vector<disperse_helper> helper_list;
    HelperCollection() = default;
    HelperCollection(HelperCollection const&) = delete;
};
*/


/* Singleton class of data vector
 * ==============================
 * Attributes:
 *  - Nobs: number of observations
 *  - data_list: a list of observed images
 *  - noise_list: a list of observed image noise
 *  -
 * */
    class DataVector {
    public:
        /* public interface */
        /* add new observation (including data, noise, and dispersion helper) */
        void add_observation(const ndarray data, const ndarray noise,
                             const disperse_helper &item);

        void clear_observation();

        double get_chi2(int index, const ndarray modelImage) const;

        int get_Nobs() const { return this->Nobs; }

        const disperse_helper &get_helper(int index) const {
            return this->helper_list.at(index);
        }

        const vector<double> &get_data(int index) const {
            return this->data_list.at(index);
        }

        const vector<double> &get_noise(int index) const {
            return this->noise_list.at(index);
        }

        /* singleton class initialization & deconstruction */
        static DataVector &get_instance() {
            static DataVector instance;
            auto pp = addressof(instance);
            //cout << "DataVector Instance addr = " << pp << endl;
            return instance;
        }

        ~DataVector() = default;

    private:
        int Nobs = 0;
        vector <vector<double>> data_list;
        vector <vector<double>> noise_list;
        vector <disperse_helper> helper_list;    // grism dispersion helper
        /* singleton class constructor */
        DataVector() = default;

        DataVector(DataVector const &) = delete;
    };

    void DataVector::add_observation(const ndarray data, const ndarray noise,
                                     const disperse_helper &item) {
        // check data and noise dimension
        py::buffer_info buf_data = data.request();
        py::buffer_info buf_noise = noise.request();

        int _Nx = item.getNx();
        int _Ny = item.getNy();
        if (buf_data.ndim != 2 || buf_noise.ndim != 2)
            throw runtime_error("Both data and noise dimension must be 2!");
        if (buf_data.shape[0] != _Ny || buf_noise.shape[0] != _Ny)
            throw runtime_error("Both data and noise must have the same Ny!");
        if (buf_data.shape[1] != _Nx || buf_noise.shape[1] != _Nx)
            throw runtime_error("Both data and noise must have the same Nx!");
        // get pointer to the buffer data memory
        auto *ptr_data = static_cast<double *>(buf_data.ptr);
        auto *ptr_noise = static_cast<double *>(buf_noise.ptr);
        // copy the data, noise array & push to the list
        vector<double> data_vec(_Ny * _Nx, 0.0);
        vector<double> noise_vec(_Ny * _Nx, 0.0);
        for (int i = 0; i < buf_data.size; i++) {
            data_vec[i] = ptr_data[i];
            noise_vec[i] = ptr_noise[i];
        }
        this->data_list.push_back(data_vec);
        this->noise_list.push_back(noise_vec);
        this->helper_list.push_back(item);
        Nobs++;
        cout << "[" << _MPI_RANK << "/" << _MPI_SIZE << "] ";
        cout << Nobs << " observations in this list" << endl;
    }

    void DataVector::clear_observation() {
        while (this->Nobs > 0) {

            this->data_list.pop_back();
            this->noise_list.pop_back();
            this->helper_list.pop_back();
            this->Nobs -= 1;
            //cout << this->Nobs << "observations left" << endl;
        }
    }

    double DataVector::get_chi2(int index, const ndarray modelImage) const {
        // check data and noise dimension
        py::buffer_info buf_modelImage = modelImage.request();
        const disperse_helper &item = this->get_helper(index);
        const vector<double> &data = this->get_data(index);
        const vector<double> &noise = this->get_noise(index);
        int _Nx = item.getNx();
        int _Ny = item.getNy();
        if (buf_modelImage.ndim != 2)
            throw runtime_error("modelImage dimension must be 2!");
        if ((buf_modelImage.shape[0] != _Ny) || (buf_modelImage.shape[1] != _Nx))
            throw runtime_error("Incorrect modelImage dimension!");
        auto *ptr_modelImage = static_cast<double *>(buf_modelImage.ptr);
        // calculate chi2
        double chi2 = 0.0;
        for (int i = 0; i < data.size(); i++) {
            chi2 += pow((ptr_modelImage[i] - data[i]) / noise[i], 2.0);
        }
        return chi2;
    }

}// interface_mpp_aux
// =============================================================================
namespace ima = interface_mpp_aux;

void cpp_add_grism_observation(const py::dict &config,
                         const ndarray lambdas,
                         const ndarray bandpasses,
                         const ndarray data, const ndarray noise)
{
    ima::DataVector& instance = ima::DataVector::get_instance();
    const ima::disperse_helper item = ima::disperse_helper(config, lambdas, bandpasses);
    instance.add_observation(data, noise, item);
}
void cpp_add_image_observation(const py::dict &config,
                               const ndarray data, const ndarray noise)
{
    ima::DataVector& instance = ima::DataVector::get_instance();
    const ima::disperse_helper item = ima::disperse_helper(config);
    instance.add_observation(data, noise, item);
}
void cpp_clear_observation(){
    ima::DataVector& instance = ima::DataVector::get_instance();
    instance.clear_observation();
    //cout << "All existing observations cleared!!!" << endl;
}
int cpp_get_Nobs(){
    ima::DataVector& instance = ima::DataVector::get_instance();
    return instance.get_Nobs();
}
void cpp_get_dispersed_image(int index, const ndarray theory_data,
                             ndarray dispersed_data){
    const ima::DataVector& instance = ima::DataVector::get_instance();
    int _Nobs = instance.get_Nobs();
    //cout << "cpp_get_dispersed_image "<< index << " out of " << _Nobs << endl;
    assert(index < _Nobs);
    const ima::disperse_helper& item = instance.get_helper(index);
    item.get_dispersed_image(theory_data, dispersed_data);
}

double cpp_get_chi2(int index, const ndarray modelImage){
    const ima::DataVector& instance = ima::DataVector::get_instance();
    return instance.get_chi2(index, modelImage);
}

void cpp_set_mpi_info(int size, int rank){
    _MPI_RANK = rank;_MPI_SIZE = size;
}
/* PYBIND11 Python Wrapper
 * */
PYBIND11_MODULE(kltools_grism_module_2, m) {

  m.doc() = "cpp grism module"; // optional module docstring
/*
  py::class_<disperse_helper>(m, "DisperseHelper")
    .def(py::init<const py::dict &, const ndarray, const ndarray >())
    .def("setDisperseHelper", &disperse_helper::set_disperse_helper)
    .def("getDispersedImage", &disperse_helper::get_dispersed_image);
*/
  m.def("get_dispersed_image",
        &cpp_get_dispersed_image,
        "Get dispersed grism image",
        py::arg("index"),
        py::arg("theory_data"),
        py::arg("dispersed_image"));

  m.def("add_grism_observation",
        &cpp_add_grism_observation,
        "Add data vector (observed images) to the C++ singleton",
        py::arg("config"),
        py::arg("lambdas"),
        py::arg("bandpasses"),
        py::arg("data"),
        py::arg("noise"));

  m.def("add_image_observation",
        &cpp_add_image_observation,
        "Add data vector (observed images) to the C++ singleton",
        py::arg("config"),
        py::arg("data"),
        py::arg("noise"));

  m.def("get_chi2",
        &cpp_get_chi2,
        "Get the chi2 for the ith observation with the input model image",
        py::arg("index"),
        py::arg("modelImage"));

  /* Test how global variable works with Python MPI */

  m.def("set_parcel",
        &set_parcel,
        "Set the global_parcel object",
        py::arg("x"),
        py::arg("y"),
        py::arg("z"),
        py::arg("value"));
  m.def("print_parcel",
        &print_parcel,
        "Print the global_parcel object");

  m.def("clear_observation",
        &cpp_clear_observation,
        "Clear all the existing observations");
  m.def("get_Nobs",
        &cpp_get_Nobs,
        "return the number of existing observations");
  m.def("set_mpi_info",
        &cpp_set_mpi_info,
        "Set the MPI rank info",
        py::arg("size"),
        py::arg("rank"));

  #ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
  #else
  m.attr("__version__") = "dev";
  #endif
}


