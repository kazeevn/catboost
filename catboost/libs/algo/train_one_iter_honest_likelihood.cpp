#include "train_templ.h"

template void TrainOneIter<THonestLikelihoodError>(const TDataset&, const TDatasetPtrs&, TLearnContext*);
