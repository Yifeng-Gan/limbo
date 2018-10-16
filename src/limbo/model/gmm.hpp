//| Copyright Inria May 2015
//| This project has received funding from the European Research Council (ERC) under
//| the European Union's Horizon 2020 research and innovation programme (grant
//| agreement No 637972) - see http://www.resibots.eu
//|
//| Contributor(s):
//|   - Jean-Baptiste Mouret (jean-baptiste.mouret@inria.fr)
//|   - Antoine Cully (antoinecully@gmail.com)
//|   - Konstantinos Chatzilygeroudis (konstantinos.chatzilygeroudis@inria.fr)
//|   - Federico Allocati (fede.allocati@gmail.com)
//|   - Vaios Papaspyros (b.papaspyros@gmail.com)
//|   - Roberto Rama (bertoski@gmail.com)
//|
//| This software is a computer library whose purpose is to optimize continuous,
//| black-box functions. It mainly implements Gaussian processes and Bayesian
//| optimization.
//| Main repository: http://github.com/resibots/limbo
//| Documentation: http://www.resibots.eu/limbo
//|
//| This software is governed by the CeCILL-C license under French law and
//| abiding by the rules of distribution of free software.  You can  use,
//| modify and/ or redistribute the software under the terms of the CeCILL-C
//| license as circulated by CEA, CNRS and INRIA at the following URL
//| "http://www.cecill.info".
//|
//| As a counterpart to the access to the source code and  rights to copy,
//| modify and redistribute granted by the license, users are provided only
//| with a limited warranty  and the software's author,  the holder of the
//| economic rights,  and the successive licensors  have only  limited
//| liability.
//|
//| In this respect, the user's attention is drawn to the risks associated
//| with loading,  using,  modifying and/or developing or reproducing the
//| software by the user in light of its specific status of free software,
//| that may mean  that it is complicated to manipulate,  and  that  also
//| therefore means  that it is reserved for developers  and  experienced
//| professionals having in-depth computer knowledge. Users are therefore
//| encouraged to load and test the software's suitability as regards their
//| requirements in conditions enabling the security of their systems and/or
//| data to be ensured and,  more generally, to use and operate it in the
//| same conditions as regards security.
//|
//| The fact that you are presently reading this means that you have had
//| knowledge of the CeCILL-C license and that you accept its terms.
//|
#ifndef LIMBO_MODEL_GMM_HPP
#define LIMBO_MODEL_GMM_HPP

#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <Eigen/LU>

// Quick hack for definition of 'I' in <complex.h>
#undef I

#include <limbo/model/gmm/em_opt.hpp>

namespace limbo {
    namespace defaults {
        struct model_gmm {
            /// @ingroup model_gmm_defaults
            BO_PARAM(bool, full_covariances, true);
        };
    } // namespace defaults

    namespace model {
        template <typename Params, typename Optimization = gmm::EMOpt<Params>>
        class GMM {
        public:
            struct GM {
            public:
                GM() {}
                GM(const Eigen::VectorXd& mu, const Eigen::MatrixXd& sigma) : _mu(mu), _sigma(sigma) {}

                Eigen::VectorXd& mu() { return _mu; }
                const Eigen::VectorXd& mu() const { return _mu; }

                Eigen::MatrixXd& sigma() { return _sigma; }
                const Eigen::MatrixXd& sigma() const { return _sigma; }

                Eigen::VectorXd params() const
                {
                    Eigen::VectorXd th = Eigen::VectorXd::Zero(_mu.size() + _sigma.size());

                    th.head(_mu.size()) = _mu;
                    th.tail(_sigma.size()) = Eigen::VectorXd::Map(_sigma.data(), _sigma.size());

                    return th;
                }

                void set_params(const Eigen::VectorXd& th)
                {
                    int d = 0.5 * (std::sqrt(4 * th.size() + 1) - 1);

                    _mu = th.head(d);
                    _sigma = Eigen::MatrixXd::Map(th.tail(d * d).data(), d, d);
                }

                double prob(const Eigen::VectorXd& x) const
                {
                    Eigen::FullPivLU<Eigen::MatrixXd> PivLU(_sigma);
                    int d = x.size();
                    double det = PivLU.determinant();
                    if (det < 0)
                        det = 0.;
                    Eigen::MatrixXd invS = PivLU.inverse();

                    Eigen::VectorXd e = (-0.5 * ((x - _mu).transpose() * invS * (x - _mu)).array()).exp(); // this should always be 1-d
                    return (1. / (std::pow(2. * M_PI, static_cast<double>(d / 2.)) * (std::sqrt(det) + 1e-50))) * e[0];
                }

            protected:
                Eigen::VectorXd _mu;
                Eigen::MatrixXd _sigma;
            };

        public:
            GMM() : _dim_in(-1), _dim_out(-1), _K(-1) {}
            GMM(int dim_in, int dim_out, int K) : _dim_in(dim_in), _dim_out(dim_out), _K(K)
            {
                _models.resize(K);
            }

            /// Compute the GP from samples and observations. This call needs to be explicit!
            void compute(const std::vector<Eigen::VectorXd>& samples,
                const std::vector<Eigen::VectorXd>& observations, int K = -1)
            {
                assert(samples.size() != 0);
                assert(observations.size() != 0);
                assert(samples.size() == observations.size());

                if (_dim_in != samples[0].size()) {
                    _dim_in = samples[0].size();
                }

                if (_dim_out != observations[0].size()) {
                    _dim_out = observations[0].size();
                }

                if (K > 0) {
                    if (K != _K)
                        _models.resize(K);
                    _K = K;
                }

                // store data
                int D = _dim_in + _dim_out;
                _data.resize(samples.size(), D);
                for (size_t i = 0; i < samples.size(); i++) {
                    _data.row(i).head(_dim_in) = samples[i];
                    _data.row(i).tail(_dim_out) = observations[i];
                }

                // begin from random mean and unit covariances
                _weights = Eigen::VectorXd::Ones(_K) / static_cast<double>(_K);
                for (int k = 0; k < _K; k++) {
                    _models[k].mu() = Eigen::VectorXd::Random(D);
                    _models[k].sigma() = Eigen::MatrixXd::Identity(D, D);
                }

                _optimizer(*this);

                std::cout << "---------------------" << std::endl;
                std::cout << "---------------------" << std::endl;
                std::cout << "---------------------" << std::endl;
                for (int k = 0; k < _K; k++) {
                    double pi_k = _weights(k);

                    Eigen::VectorXd mu = _models[k].mu();
                    Eigen::MatrixXd S = _models[k].sigma();

                    std::cout << pi_k << " " << mu.transpose() << std::endl;
                    std::cout << S << std::endl;
                    std::cout << "---------------------" << std::endl;
                }
            }

            /// return the prediction at point x
            Eigen::VectorXd mu(const Eigen::VectorXd& x) const
            {
                assert(_data.size());
                assert(_dim_in != -1);
                assert(_dim_out != -1);

                int d = _dim_out;
                int D = _dim_in;

                Eigen::VectorXd out = Eigen::VectorXd::Zero(d);
                Eigen::VectorXd probs = Eigen::VectorXd::Zero(_K);

                for (int k = 0; k < _K; k++) {
                    GM gm_k(_models[k].mu().head(D), _models[k].sigma().block(0, 0, D, D));
                    probs(k) = _weights[k] * gm_k.prob(x);
                }

                for (int k = 0; k < _K; k++) {
                    Eigen::MatrixXd sigma = _models[k].sigma();
                    Eigen::MatrixXd sigma_x = sigma.block(0, 0, D, D);
                    Eigen::FullPivLU<Eigen::MatrixXd> PivLU(sigma_x);
                    Eigen::MatrixXd inv_sigma_x = PivLU.inverse();
                    // Eigen::MatrixXd sigma_xy = sigma.block(0, D, D, d);
                    Eigen::MatrixXd sigma_yx = sigma.block(D, 0, d, D);
                    // Eigen::MatrixXd sigma_y = sigma.block(D, D, d, d);

                    Eigen::MatrixXd res = sigma_yx * inv_sigma_x * (x - _models[k].mu().head(D));

                    out.array() += probs(k) / probs.sum() * (_models[k].mu().tail(d).array() + res.array());
                }

                return out;
            }

            /// return the variance prediction at point x
            Eigen::VectorXd sigma(const Eigen::VectorXd& x) const
            {
                assert(_data.size());
                assert(_dim_in != -1);
                assert(_dim_out != -1);

                int d = _dim_out;
                int D = _dim_in;

                Eigen::VectorXd out = Eigen::VectorXd::Zero(d);
                Eigen::VectorXd out2 = Eigen::VectorXd::Zero(d);
                Eigen::VectorXd probs = Eigen::VectorXd::Zero(_K);

                for (int k = 0; k < _K; k++) {
                    GM gm_k(_models[k].mu().head(D), _models[k].sigma().block(0, 0, D, D));
                    probs(k) = _weights[k] * gm_k.prob(x);
                }

                Eigen::MatrixXd means = Eigen::MatrixXd::Zero(_K, d);
                Eigen::MatrixXd sigmas = Eigen::MatrixXd::Zero(_K, d);

                for (int k = 0; k < _K; k++) {
                    Eigen::MatrixXd sigma = _models[k].sigma();
                    Eigen::MatrixXd sigma_x = sigma.block(0, 0, D, D);
                    Eigen::FullPivLU<Eigen::MatrixXd> PivLU(sigma_x);
                    Eigen::MatrixXd inv_sigma_x = PivLU.inverse();
                    Eigen::MatrixXd sigma_xy = sigma.block(0, D, D, d);
                    Eigen::MatrixXd sigma_yx = sigma.block(D, 0, d, D);
                    Eigen::MatrixXd sigma_y = sigma.block(D, D, d, d);

                    Eigen::MatrixXd res = sigma_yx * inv_sigma_x * (x - _models[k].mu().head(D));

                    means.row(k) = (_models[k].mu().tail(d).array() + res.array());
                    sigmas.row(k) = sigma_y - sigma_yx * inv_sigma_x * sigma_xy;
                }

                for (int k = 0; k < _K; k++) {
                    double w = probs(k) / probs.sum();
                    out.array() += w * (means.row(k).array().square() + sigmas.row(k).array());
                    out2.array() += w * means.row(k).array();
                }

                return out.array() - out2.array().square();
            }

            /// return the number of dimensions of the input
            int dim_in() const
            {
                assert(_dim_in != -1); // need to compute first!
                return _dim_in;
            }

            /// return the number of dimensions of the output
            int dim_out() const
            {
                assert(_dim_out != -1); // need to compute first!
                return _dim_out;
            }

            /// return the number of models
            int K() const
            {
                assert(_K != -1);
                return _K;
            }

            /// return the data
            const Eigen::MatrixXd& data() const
            {
                return _data;
            }

            std::vector<GM>& models() { return _models; }
            const std::vector<GM>& models() const { return _models; }

            const Eigen::VectorXd& weights() const { return _weights; }
            Eigen::VectorXd& weights() { return _weights; }

            Eigen::VectorXd params() const
            {
                Eigen::VectorXd p = _weights;
                for (int k = 0; k < _K; k++) {
                    Eigen::VectorXd gm_params = _models[k].params();
                    p.conservativeResize(p.size() + gm_params.size());
                    p.tail(gm_params.size()) = gm_params;
                }

                return p;
            }

            void set_params(const Eigen::VectorXd& params)
            {
                assert(_K > 0);
                Eigen::VectorXd gm_params = _models[0].params();
                int n_params = gm_params.size();
                _weights = params.head(_K);
                for (int k = 0; k < _K; k++) {
                    _models[k].set_params(params.segment(_K + k * n_params, n_params));
                }
            }

        protected:
            int _dim_in, _dim_out, _K;
            Eigen::MatrixXd _data;
            Eigen::VectorXd _weights;
            std::vector<GM> _models;
            Optimization _optimizer;

            // double _gaussian_pdf(const Eigen::VectorXd& x, const Eigen::VectorXd& mu, const Eigen::MatrixXd& S) const
            // {
            //     Eigen::FullPivLU<Eigen::MatrixXd> PivLU(S);
            //     int d = x.size();
            //     double det = PivLU.determinant();
            //     if (det < 0)
            //         det = 0.;
            //     Eigen::MatrixXd invS = PivLU.inverse();

            //     Eigen::VectorXd e = (-0.5 * ((x - mu).transpose() * invS * (x - mu)).array()).exp(); // this should always be 1-d
            //     return (1. / (std::pow(M_PI, static_cast<double>(d / 2.)) * std::sqrt(det) + 1e-25)) * e[0];
            // }

            // Eigen::VectorXd _to_params(const Eigen::VectorXd& mu, const Eigen::MatrixXd& S) const
            // {
            //     Eigen::VectorXd th = Eigen::VectorXd::Zero(mu.size() + S.size());

            //     th.head(mu.size()) = mu;
            //     th.tail(S.size()) = Eigen::VectorXd::Map(S.data(), S.size());

            //     return th;
            // }

            // std::tuple<Eigen::VectorXd, Eigen::MatrixXd> _to_dist(const Eigen::VectorXd& th) const
            // {
            //     int d = 0.5 * (std::sqrt(4 * th.size() + 1) - 1);

            //     Eigen::VectorXd mu = th.head(d);
            //     Eigen::MatrixXd S = Eigen::MatrixXd::Map(th.tail(d * d).data(), d, d);

            //     return std::make_tuple(mu, S);
            // }
        };
    } // namespace model
} // namespace limbo

#endif