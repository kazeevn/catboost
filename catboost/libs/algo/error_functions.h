#pragma once

#include "approx_util.h"

#include "hessian.h"

#include <catboost/libs/options/catboost_options.h>
#include <catboost/libs/options/enums.h>
#include <catboost/libs/metrics/ders_holder.h>
#include <catboost/libs/metrics/metric.h>
#include <catboost/libs/metrics/auc.h>
#include <catboost/libs/data_types/pair.h>
#include <catboost/libs/eval_result/eval_helpers.h>

#include <library/containers/2d_array/2d_array.h>
#include <library/fast_exp/fast_exp.h>
#include <library/threading/local_executor/local_executor.h>
#include <library/binsaver/bin_saver.h>

#include <util/generic/vector.h>
#include <util/generic/ymath.h>
#include <util/system/yassert.h>
#include <util/string/iterator.h>

template<typename TChild, bool StoreExpApproxParam>
class IDerCalcer {
public:
    static constexpr bool StoreExpApprox = StoreExpApproxParam;
    static constexpr bool IsCatboostErrorFunction = true;

    void CalcFirstDerRange(
        int start,
        int count,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        double* ders
    ) const {
        if (approxDeltas != nullptr) {
            for (int i = start; i < start + count; ++i) {
                ders[i] = CalcDer(UpdateApprox<StoreExpApprox>(approxes[i], approxDeltas[i]), targets[i]);
            }
        } else {
            for (int i = start; i < start + count; ++i) {
                ders[i] = CalcDer(approxes[i], targets[i]);
            }
        }
        if (weights != nullptr) {
            for (int i = start; i < start + count; ++i) {
                ders[i] *= static_cast<double>(weights[i]);
            }
        }
    }

    void CalcDersRange(
        int start,
        int count,
        bool calcThirdDer,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        TDers* ders
    ) const {
        if (approxDeltas != nullptr) {
            if (calcThirdDer) {
                for (int i = start; i < start + count; ++i) {
                    CalcDers<true>(UpdateApprox<StoreExpApprox>(approxes[i], approxDeltas[i]), targets[i], &ders[i]);
                }
            } else {
                for (int i = start; i < start + count; ++i) {
                    CalcDers<false>(UpdateApprox<StoreExpApprox>(approxes[i], approxDeltas[i]), targets[i], &ders[i]);
                }
            }
        } else {
            if (calcThirdDer) {
                for (int i = start; i < start + count; ++i) {
                    CalcDers<true>(approxes[i], targets[i], &ders[i]);
                }
            } else {
                for (int i = start; i < start + count; ++i) {
                    CalcDers<false>(approxes[i], targets[i], &ders[i]);
                }
            }
        }
        if (weights != nullptr) {
            if (calcThirdDer) {
                for (int i = start; i < start + count; ++i) {
                    ders[i].Der1 *= weights[i];
                    ders[i].Der2 *= weights[i];
                    ders[i].Der3 *= weights[i];
                }
            } else {
                for (int i = start; i < start + count; ++i) {
                    ders[i].Der1 *= weights[i];
                    ders[i].Der2 *= weights[i];
                }
            }
        }
    }

    void CalcDersMulti(
        const TVector<double>& /*approx*/,
        float /*target*/,
        float /*weight*/,
        TVector<double>* /*der*/,
        THessianInfo* /*der2*/
    ) const {
        CB_ENSURE(false, "Not implemented");
    }

    void CalcDersForQueries(
        int /*queryStartIndex*/,
        int /*queryEndIndex*/,
        const TVector<double>& /*approx*/,
        const TVector<float>& /*target*/,
        const TVector<float>& /*weight*/,
        const TVector<TQueryInfo>& /*queriesInfo*/,
        TVector<TDers>* /*ders*/,
        NPar::TLocalExecutor* /*localExecutor*/
    ) const {
        CB_ENSURE(false, "Not implemented");
    }

    EErrorType GetErrorType() const {
        return EErrorType::PerObjectError;
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 3;
    }

    static constexpr EHessianType GetHessianType() {
        return EHessianType::Symmetric;
    }

private:
    double CalcDer(double approx, float target) const {
        return static_cast<const TChild*>(this)->CalcDer(approx, target);
    }

    double CalcDer2(double approx, float target) const {
        return static_cast<const TChild*>(this)->CalcDer2(approx, target);
    }

    double CalcDer3(double approx, float target) const {
        return static_cast<const TChild*>(this)->CalcDer3(approx, target);
    }

    template<bool CalcThirdDer>
    void CalcDers(double approx, float target, TDers* ders) const {
        ders->Der1 = CalcDer(approx, target);
        ders->Der2 = CalcDer2(approx, target);
        if (CalcThirdDer) {
            ders->Der3 = CalcDer3(approx, target);
        }
    }
};

class TCrossEntropyError : public IDerCalcer<TCrossEntropyError, /*StoreExpApproxParam*/ true> {
public:
    explicit TCrossEntropyError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approxExp, float target) const {
        const double p = approxExp / (1 + approxExp);
        return target - p;
    }

    double CalcDer2(double approxExp, float = 0) const {
        const double p = approxExp / (1 + approxExp);
        return -p * (1 - p);
    }

    double CalcDer3(double approxExp, float = 0) const {
        const double p = approxExp / (1 + approxExp);
        return -p * (1 - p) * (1 - 2 * p);
    }

    template<bool CalcThirdDer>
    void CalcDers(double approxExp, float target, TDers* ders) const {
        const double p = approxExp / (1 + approxExp);
        ders->Der1 = target - p;
        ders->Der2 = -p * (1 - p);
        if (CalcThirdDer) {
            ders->Der3 = -p * (1 - p) * (1 - 2 * p);
        }
    }

    void CalcFirstDerRange(
        int start,
        int count,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        double* ders
    ) const;

    void CalcDersRange(
        int start,
        int count,
        bool calcThirdDer,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        TDers* ders
    ) const;
};

class TConstrainedRegressionError : public IDerCalcer<TConstrainedRegressionError, /*StoreExpApproxParam*/ true> {
public:
    explicit TConstrainedRegressionError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    // L = (t - e^x/(1 + e^x))^2
    // dL/dX = -(2 e^x (t e^x + t - e^x))\/(e^x + 1)^3
    double CalcDer(double approxExp, float target) const {
	const double a_plus_one = approxExp + 1.;
        return 2.*approxExp*((target-1.)*approxExp + target)/(a_plus_one*a_plus_one*a_plus_one);
    }


    // (2 e^x ((t - 1) e^(2 x) - t + 2 e^x))\/(e^x + 1)^4
    double CalcDer2(double approxExp, float target) const {
	const double a_plus_one = approxExp + 1.;
	const double a_plus_one_squared = a_plus_one * a_plus_one;
	return -2*approxExp*((target - 1.)*approxExp*approxExp - target + 2*approxExp) / \
	    (a_plus_one_squared*a_plus_one_squared);
    }

    // (2 e^x (e^x (-e^x ((t - 1) e^x - 3 t + 7) + 3 t + 4) - t))\/(e^x + 1)^5
    double CalcDer3(double approxExp, float target) const {
	const double a_plus_one = approxExp + 1.;
	const double a_plus_one_squared = a_plus_one * a_plus_one;
	return -2*approxExp*(approxExp*(-approxExp*((target - 1.)*approxExp - 3*target + 7.) +
				       3*target + 4.) - target) / \
	    (a_plus_one_squared*a_plus_one_squared*a_plus_one);
    }

    template<bool CalcThirdDer>
    void CalcDers(double approxExp, float target, TDers* ders) const {
	const double a_plus_one = approxExp + 1.;
	const double a_plus_one_squared = a_plus_one * a_plus_one;
	const double a_plus_one_4th = a_plus_one_squared*a_plus_one_squared;
        ders->Der1 = \
	    2.*approxExp*((target-1.)*approxExp + target)/(a_plus_one*a_plus_one*a_plus_one);
        ders->Der2 = -2*approxExp*((target - 1.)*approxExp*approxExp - target + 2*approxExp) / \
	    (a_plus_one_4th);
        if (CalcThirdDer) {
            ders->Der3 = \
		-2*approxExp*(approxExp*(-approxExp*((target - 1.)*approxExp - 3*target + 7.) +
					3*target + 4.) - target) /	\
		(a_plus_one_4th*a_plus_one);
        }
    }
};

class THonestLikelihoodError : public IDerCalcer<THonestLikelihoodError, /*StoreExpApproxParam*/ true> {
public:
    explicit THonestLikelihoodError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    // L = -log(t * sigmoid(predictions) + (1 - t) * sigmoid(-predictions))
    double CalcDer(double approxExp, float target) const {
	const double rp = approxExp/(1. + approxExp);
	const double rnp = 1./(1. + approxExp);
	const double rp_grad = rp*rnp;
	return rp_grad*(2*target - 1) / (target*rp + (1 - target)*rnp);
    }

    double CalcDer2(double approxExp, float target) const {
	const double rp = approxExp/(1. + approxExp);
	const double rnp = 1./(1. + approxExp);
	const double rp_grad = rp*rnp;
	const double A = rp_grad * (2 * target - 1);
	const double B = target * rp + (1 - target) * rnp;
	return -(A*A/(B*B) + rp_grad*(2*target - 1) * (rp - rnp)/B);
    }

    double CalcDer3(double approxExp, float target) const {
	const double denomRoot3 = (approxExp + 1) * (target * approxExp - target + 1);
	const double approxExp2 = approxExp*approxExp;
	const double target2 = target*target;
	return (2*target - 1)*approxExp*(target2*approxExp2*approxExp2 -
					  target*approxExp2*approxExp +
					  6*target2*approxExp2 -
					  6*target*approxExp2 +
					  target*approxExp -
					  approxExp +
					  target2 -
					  2*target + 1.) / pow(denomRoot3, 3);
    }

    template<bool CalcThirdDer>
    void CalcDers(double approxExp, float target, TDers* ders) const {
;	const double rp = approxExp/(1. + approxExp);
	const double rnp = 1./(1. + approxExp);
	const double rp_grad = rp*rnp;
	const double A = rp_grad * (2 * target - 1);
	const double B = target * rp + (1 - target) * rnp;
	ders->Der1 = A/B;
	ders->Der2 = -(A*A/(B*B) + rp_grad*(2*target - 1) * (rp - rnp)/B);
        if (CalcThirdDer) {
	    const double target2 = target*target;
	    const double approxExp2 = approxExp*approxExp;
	    const double denomRoot3 = (approxExp + 1) * (target * approxExp - target + 1);
	    ders->Der3 = (2*target - 1)*approxExp*(target2*approxExp2*approxExp2 -
						   target*approxExp2*approxExp +
						   6*target2*approxExp2 -
						   6*target*approxExp2 +
						   target*approxExp -
						   approxExp +
						   target2 -
						   2*target + 1.) / pow(denomRoot3, 3);
        }
    }
};

class TRMSEError : public IDerCalcer<TRMSEError, /*StoreExpApproxParam*/ false> {
public:
    static constexpr double RMSE_DER2 = -1.0;
    static constexpr double RMSE_DER3 = 0.0;

    explicit TRMSEError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approx, float target) const {
        return target - approx;
    }

    double CalcDer2(double = 0, float = 0) const {
        return RMSE_DER2;
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        return RMSE_DER3;
    }
};

class TQuantileError : public IDerCalcer<TQuantileError, /*StoreExpApproxParam*/ false> {
public:
    const double QUANTILE_DER2_AND_DER3 = 0.0;

    double Alpha;
    SAVELOAD(Alpha);

    explicit TQuantileError(bool storeExpApprox)
        : Alpha(0.5)
    {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    TQuantileError(double alpha, bool storeExpApprox)
        : Alpha(alpha)
    {
        Y_ASSERT(Alpha > -1e-6 && Alpha < 1.0 + 1e-6);
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approx, float target) const {
        return (target - approx > 0) ? Alpha : -(1 - Alpha);
    }

    double CalcDer2(double = 0, float = 0) const {
        return QUANTILE_DER2_AND_DER3;
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        return QUANTILE_DER2_AND_DER3;
    }
};

class TLqError : public IDerCalcer<TLqError, /*StoreExpApproxParam*/ false> {
public:
    double Q;
    SAVELOAD(Q);

    TLqError(double q, bool storeExpApprox)
            : Q(q)
    {
        Y_ASSERT(Q >= 1);
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approx, float target) const {
        const double absLoss = abs(approx - target);
        const double absLossQ = std::pow(absLoss, Q - 1);
        return Q * (approx - target > 0 ? 1 : -1)  * absLossQ;
    }

    int GetMaxSupportedDerivativeOrder() const {
        return Q >= 2 ?  3 : 1;
    }

    double CalcDer2(double approx, float target) const {
        const double absLoss = abs(target - approx);
        return Q * (Q - 1) * std::pow(absLoss, Q - 2);
    }

    double CalcDer3(double approx, float target) const {
        const double absLoss = abs(target - approx);
        return Q * (Q - 1) *  (Q - 2) * std::pow(absLoss, Q - 3) * (approx - target > 0 ? 1 : -1);
    }
};

class TLogLinQuantileError : public IDerCalcer<TLogLinQuantileError, /*StoreExpApproxParam*/ true> {
public:
    const double QUANTILE_DER2_AND_DER3 = 0.0;

    double Alpha;
    SAVELOAD(Alpha);

    explicit TLogLinQuantileError(bool storeExpApprox)
        : Alpha(0.5)
    {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    TLogLinQuantileError(double alpha, bool storeExpApprox)
        : Alpha(alpha)
    {
        Y_ASSERT(Alpha > -1e-6 && Alpha < 1.0 + 1e-6);
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approxExp, float target) const {
        return (target - approxExp > 0) ? Alpha * approxExp : -(1 - Alpha) * approxExp;
    }

    double CalcDer2(double = 0, float = 0) const {
        return QUANTILE_DER2_AND_DER3;
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        return QUANTILE_DER2_AND_DER3;
    }
};

class TMAPError : public IDerCalcer<TMAPError, /*StoreExpApproxParam*/ false> {
public:
    const double MAPE_DER2_AND_DER3 = 0.0;

    explicit TMAPError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approx, float target) const {
        return (target - approx > 0) ? 1 / target : -1 / target;
    }

    double CalcDer2(double = 0, float = 0) const {
        return MAPE_DER2_AND_DER3;
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        return MAPE_DER2_AND_DER3;
    }
};

class TPoissonError : public IDerCalcer<TPoissonError, /*StoreExpApproxParam*/ true> {
public:
    explicit TPoissonError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double approxExp, float target) const {
        return target - approxExp;
    }

    double CalcDer2(double approxExp, float) const {
        return -approxExp;
    }

    double CalcDer3(double approxExp, float /*target*/) const {
        return -approxExp;
    }

    template<bool CalcThirdDer>
    void CalcDers(double approxExp, float target, TDers* ders) const {
        ders->Der1 = target - approxExp;
        ders->Der2 = -approxExp;
        if (CalcThirdDer) {
            ders->Der3 = -approxExp;
        }
    }
};

class TMultiClassError : public IDerCalcer<TMultiClassError, /*StoreExpApproxParam*/ false> {
public:
    explicit TMultiClassError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for MultiClass error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for MultiClass error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented.");
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 2;
    }

    void CalcDersMulti(
        const TVector<double>& approx,
        float target,
        float weight,
        TVector<double>* der,
        THessianInfo* der2
    ) const {
        const int approxDimension = approx.ysize();

        TVector<double> softmax(approxDimension);
        CalcSoftmax(approx, &softmax);

        for (int dim = 0; dim < approxDimension; ++dim) {
            (*der)[dim] = -softmax[dim];
        }
        int targetClass = static_cast<int>(target);
        (*der)[targetClass] += 1;

        if (der2 != nullptr) {
            Y_ASSERT(der2->HessianType == EHessianType::Symmetric &&
                     der2->ApproxDimension == approxDimension);
            int idx = 0;
            for (int dimY = 0; dimY < approxDimension; ++dimY) {
                der2->Data[idx++] = softmax[dimY] * (softmax[dimY] - 1);
                for (int dimX = dimY + 1; dimX < approxDimension; ++dimX) {
                    der2->Data[idx++] = softmax[dimY] * softmax[dimX];
                }
            }
        }

        if (weight != 1) {
            for (int dim = 0; dim < approxDimension; ++dim) {
                (*der)[dim] *= weight;
            }
            if (der2 != nullptr) {
                int idx = 0;
                for (int dimY = 0; dimY < approxDimension; ++dimY) {
                    for (int dimX = dimY; dimX < approxDimension; ++dimX) {
                        der2->Data[idx++] *= weight;
                    }
                }
            }
        }
    }
};

class TMultiClassOneVsAllError : public IDerCalcer<TMultiClassError, /*StoreExpApproxParam*/ false> {
public:
    explicit TMultiClassOneVsAllError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for MultiClassOneVsAll error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for MultiClassOneVsAll error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented.");
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 2;
    }

    static constexpr EHessianType GetHessianType() {
        return EHessianType::Diagonal;
    }

    void CalcDersMulti(
        const TVector<double>& approx,
        float target,
        float weight,
        TVector<double>* der,
        THessianInfo* der2
    ) const {
        const int approxDimension = approx.ysize();

        TVector<double> prob = approx;
        FastExpInplace(prob.data(), prob.ysize());
        for (int dim = 0; dim < approxDimension; ++dim) {
            prob[dim] /= (1 + prob[dim]);
            (*der)[dim] = -prob[dim];
        }
        int targetClass = static_cast<int>(target);
        (*der)[targetClass] += 1;

        if (der2 != nullptr) {
            Y_ASSERT(der2->HessianType == EHessianType::Diagonal &&
                     der2->ApproxDimension == approxDimension);
            for (int dim = 0; dim < approxDimension; ++ dim) {
                der2->Data[dim] = -prob[dim] * (1 - prob[dim]);
            }
        }

        if (weight != 1) {
            for (int dim = 0; dim < approxDimension; ++dim) {
                (*der)[dim] *= weight;
            }
            if (der2 != nullptr) {
                for (int dim = 0; dim < approxDimension; ++dim) {
                    der2->Data[dim] *= weight;
                }
            }
        }
    }
};

class TPairLogitError : public IDerCalcer<TPairLogitError, /*StoreExpApproxParam*/ true> {
public:
    explicit TPairLogitError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for PairLogit error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for PairLogit error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented.");
    }

    EErrorType GetErrorType() const {
        return EErrorType::PairwiseError;
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 2;
    }

    void CalcDersForQueries(
        int queryStartIndex,
        int queryEndIndex,
        const TVector<double>& expApproxes,
        const TVector<float>& /*targets*/,
        const TVector<float>& /*weights*/,
        const TVector<TQueryInfo>& queriesInfo,
        TVector<TDers>* ders,
        NPar::TLocalExecutor* localExecutor
    ) const {
        CB_ENSURE(queryStartIndex < queryEndIndex);
        const int start = queriesInfo[queryStartIndex].Begin;
        NPar::ParallelFor(*localExecutor, queryStartIndex, queryEndIndex, [&] (ui32 queryIndex) {
            const int begin = queriesInfo[queryIndex].Begin;
            const int end = queriesInfo[queryIndex].End;
            TDers* dersData = ders->data() + begin - start;
            Fill(dersData, dersData + end - begin, TDers{/*1st*/0.0, /*2nd*/0.0, /*3rd*/0.0});
            for (int docId = begin; docId < end; ++docId) {
                double winnerDer = 0.0;
                double winnerSecondDer = 0.0;
                for (const auto& competitor : queriesInfo[queryIndex].Competitors[docId - begin]) {
                    const double p = expApproxes[competitor.Id + begin] / (expApproxes[competitor.Id + begin] + expApproxes[docId]);
                    winnerDer += competitor.Weight * p;
                    dersData[competitor.Id].Der1 -= competitor.Weight * p;
                    winnerSecondDer += competitor.Weight * p * (p - 1);
                    dersData[competitor.Id].Der2 += competitor.Weight * p * (p - 1);
                }
                dersData[docId - begin].Der1 += winnerDer;
                dersData[docId - begin].Der2 += winnerSecondDer;
            }
        });
    }
};

class TQueryRmseError : public IDerCalcer<TQueryRmseError, /*StoreExpApproxParam*/ false> {
public:
    explicit TQueryRmseError(bool storeExpApprox) {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for QueryRMSE error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for QueryRMSE error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented.");
    }

    EErrorType GetErrorType() const {
        return EErrorType::QuerywiseError;
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 2;
    }

    void CalcDersForQueries(
        int queryStartIndex,
        int queryEndIndex,
        const TVector<double>& approxes,
        const TVector<float>& targets,
        const TVector<float>& weights,
        const TVector<TQueryInfo>& queriesInfo,
        TVector<TDers>* ders,
        NPar::TLocalExecutor* localExecutor
    ) const {
        const int start = queriesInfo[queryStartIndex].Begin;
        NPar::ParallelFor(*localExecutor, queryStartIndex, queryEndIndex, [&] (ui32 queryIndex) {
            const int begin = queriesInfo[queryIndex].Begin;
            const int end = queriesInfo[queryIndex].End;
            const int querySize = end - begin;

            const double queryAvrg = CalcQueryAvrg(begin, querySize, approxes, targets, weights);
            for (int docId = begin; docId < end; ++docId) {
                (*ders)[docId - start].Der1 = targets[docId] - approxes[docId] - queryAvrg;
                (*ders)[docId - start].Der2 = -1;
                if (!weights.empty()) {
                    (*ders)[docId - start].Der1 *= weights[docId];
                    (*ders)[docId - start].Der2 *= weights[docId];
                }
            }
        });
    }
private:
    double CalcQueryAvrg(
        int start,
        int count,
        const TVector<double>& approxes,
        const TVector<float>& targets,
        const TVector<float>& weights
    ) const {
        double querySum = 0;
        double queryCount = 0;
        for (int docId = start; docId < start + count; ++docId) {
            double w = weights.empty() ? 1 : weights[docId];
            querySum += (targets[docId] - approxes[docId]) * w;
            queryCount += w;
        }

        double queryAvrg = 0;
        if (queryCount > 0) {
            queryAvrg = querySum / queryCount;
        }
        return queryAvrg;
    }
};

class TQuerySoftMaxError : public IDerCalcer<TQuerySoftMaxError, /*StoreExpApproxParam*/ false> {
public:

    double LambdaReg;
    SAVELOAD(LambdaReg);

    explicit TQuerySoftMaxError(double lambdaReg, bool storeExpApprox)
        : LambdaReg(lambdaReg)
    {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for QuerySoftMax error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for QuerySoftMax error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented.");
    }

    void CalcDersForQueries(
        int queryStartIndex,
        int queryEndIndex,
        const TVector<double>& approxes,
        const TVector<float>& targets,
        const TVector<float>& weights,
        const TVector<TQueryInfo>& queriesInfo,
        TVector<TDers>* ders,
        NPar::TLocalExecutor* localExecutor
    ) const {
        int start = queriesInfo[queryStartIndex].Begin;
        NPar::ParallelFor(*localExecutor, queryStartIndex, queryEndIndex, [&](int queryIndex) {
            int begin = queriesInfo[queryIndex].Begin;
            int end = queriesInfo[queryIndex].End;
            CalcDersForSingleQuery(start, begin - start, end - begin, approxes, targets, weights, *ders);
        });
    }

    EErrorType GetErrorType() const {
        return EErrorType::QuerywiseError;
    }

    ui32 GetMaxSupportedDerivativeOrder() const {
        return 2;
    }

private:
    void CalcDersForSingleQuery(
        int start,
        int offset,
        int count,
        TConstArrayRef<double> approxes,
        TConstArrayRef<float> targets,
        TConstArrayRef<float> weights,
        TArrayRef<TDers> ders
    ) const;
};

class TCustomError : public IDerCalcer<TCustomError, /*StoreExpApproxParam*/ false> {
public:
    TCustomError(
        const NCatboostOptions::TCatBoostOptions& params,
        const TMaybe<TCustomObjectiveDescriptor>& descriptor
    )
        : Descriptor(*descriptor)
    {
        CB_ENSURE(IsStoreExpApprox(params.LossFunctionDescription->GetLossFunction()) == StoreExpApprox, "Approx format does not match");
    }

    void CalcDersMulti(
        const TVector<double>& approx,
        float target,
        float weight,
        TVector<double>* der,
        THessianInfo* der2
    ) const {
        Descriptor.CalcDersMulti(approx, target, weight, der, der2, Descriptor.CustomData);
    }

    void CalcDersRange(
        int start,
        int count,
        bool /*calcThirdDer*/,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        TDers* ders
    ) const {
        memset(ders + start, 0, sizeof(*ders) * count);
        if (approxDeltas != nullptr) {
            TVector<double> updatedApproxes(count);
            for (int i = start; i < start + count; ++i) {
                updatedApproxes[i - start] = approxes[i] + approxDeltas[i];
            }
            Descriptor.CalcDersRange(count, updatedApproxes.data(), targets + start, weights + start, ders + start, Descriptor.CustomData);
        } else {
            Descriptor.CalcDersRange(count, approxes + start, targets + start, weights + start, ders + start, Descriptor.CustomData);
        }
    }

    void CalcFirstDerRange(
        int start,
        int count,
        const double* approxes,
        const double* approxDeltas,
        const float* targets,
        const float* weights,
        double* ders
    ) const {
        TVector<TDers> derivatives(count, {0.0, 0.0, 0.0});
        CalcDersRange(start, count, /*calcThirdDer=*/false, approxes, approxDeltas, targets, weights, derivatives.data() - start);
        for (int i = start; i < start + count; ++i) {
            ders[i] = derivatives[i - start].Der1;
        }
    }
private:
    TCustomObjectiveDescriptor Descriptor;
};

class TUserDefinedPerObjectError : public IDerCalcer<TUserDefinedPerObjectError, /*StoreExpApproxParam*/ false> {
public:

    double Alpha;
    SAVELOAD(Alpha);

    TUserDefinedPerObjectError(const TMap<TString, TString>& params, bool storeExpApprox)
        : Alpha(0.0)
    {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
        if (params.has("alpha")) {
            Alpha = FromString<double>(params.at("alpha"));
        }
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedPerObjectError error.");
        return 0.0;
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedPerObjectError error.");
        return 0.0;
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedPerObjectError error.");
        return 0.0;
    }
};

class TUserDefinedQuerywiseError : public IDerCalcer<TUserDefinedQuerywiseError, /*StoreExpApproxParam*/ false> {
public:

    double Alpha;
    SAVELOAD(Alpha);

    TUserDefinedQuerywiseError(const TMap<TString, TString>& params, bool storeExpApprox)
        : Alpha(0.0)
    {
        CB_ENSURE(storeExpApprox == StoreExpApprox, "Approx format does not match");
        if (params.has("alpha")) {
            Alpha = FromString<double>(params.at("alpha"));
        }
    }

    double CalcDer(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedQuerywiseError error.");
    }

    double CalcDer2(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedQuerywiseError error.");
    }

    double CalcDer3(double /*approx*/, float /*target*/) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedQuerywiseError error.");
    }

    void CalcDersForQueries(
        int /*queryStartIndex*/,
        int /*queryEndIndex*/,
        const TVector<double>& /*approx*/,
        const TVector<float>& /*target*/,
        const TVector<float>& /*weight*/,
        const TVector<TQueryInfo>& /*queriesInfo*/,
        TVector<TDers>* /*ders*/,
        NPar::TLocalExecutor* /*localExecutor*/
    ) const {
        CB_ENSURE(false, "Not implemented for TUserDefinedQuerywiseError error.");
    }

    EErrorType GetErrorType() const {
        return EErrorType::QuerywiseError;
    }
};


void CheckDerivativeOrderForTrain(ui32 derivativeOrder, ELeavesEstimation estimationMethod);
void CheckDerivativeOrderForObjectImportance(ui32 derivativeOrder, ELeavesEstimation estimationMethod);
