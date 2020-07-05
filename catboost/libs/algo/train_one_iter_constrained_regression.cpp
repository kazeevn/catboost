#include "train_templ.h"

template void TrainOneIter<TConstrainedRegressionError>(const TDataset&, const TDatasetPtrs&, TLearnContext*);
