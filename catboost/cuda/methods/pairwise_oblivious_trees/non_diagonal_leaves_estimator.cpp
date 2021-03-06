#include "non_diagonal_leaves_estimator.h"

void NCatboostCuda::TPairwiseObliviousTreeLeavesEstimator::Estimate(ui32 taskId) {
    auto& task = Tasks.at(taskId);
    auto derCalcer = CreateDerCalcer(task);

    TNewtonLikeWalker<INonDiagonalOracle> newtonLikeWalker(*derCalcer,
                                                           LeavesEstimationConfig.Iterations,
                                                           LeavesEstimationConfig.BacktrackingType);

    TVector<float> point;
    TVector<float> weights;
    point.resize(task.Model->GetStructure().LeavesCount());
    point = newtonLikeWalker.Estimate(point);
    //for pure pairwise modes we remove 1 row from point
    derCalcer->WriteWeights(&weights);
    point.resize(task.Model->GetStructure().LeavesCount());
    Y_VERIFY(point.size() == weights.size());

    if (LeavesEstimationConfig.MakeZeroAverage) {
        double sum = 0;
        double weight = 0;
        for (size_t i = 0; i < point.size(); ++i) {
            sum += point[i];
            weight += 1;
        }
        const double bias = weight > 0 ? -sum / weight : 0;

        for (size_t i = 0; i < point.size(); ++i) {
            point[i] += bias;
        }
    }
    task.Model->UpdateLeaves(std::move(point));
    task.Model->UpdateLeavesWeights(std::move(weights));
}

THolder<NCatboostCuda::INonDiagonalOracle> NCatboostCuda::TPairwiseObliviousTreeLeavesEstimator::CreateDerCalcer(const NCatboostCuda::TPairwiseObliviousTreeLeavesEstimator::TTask& task) {
    const ui32 binCount = static_cast<ui32>(task.Model->GetStructure().LeavesCount());
    auto bins = TStripeBuffer<ui32>::CopyMapping(task.Cursor);
    {
        auto guard = NCudaLib::GetCudaManager().GetProfiler().Profile("Compute bins doc-parallel");
        ComputeBinsForModel(task.Model->GetStructure(),
                            *task.DataSet,
                            &bins);
    }
    return task.DerCalcerFactory->Create(LeavesEstimationConfig,
                                         task.Cursor.ConstCopyView(),
                                         std::move(bins),
                                         binCount);
}
