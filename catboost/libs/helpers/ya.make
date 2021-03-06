LIBRARY()



SRCS(
    binarize_target.cpp
    data_split.cpp
    dense_hash.cpp
    dense_hash_view.cpp
    eval_helpers.cpp
    interrupt.cpp
    matrix.cpp
    power_hash.cpp
    progress_helper.cpp
    permutation.cpp
    query_info_helper.cpp
    restorable_rng.cpp
    multiclass_label_helpers/visible_label_helper.cpp
    multiclass_label_helpers/label_converter.cpp
    multiclass_label_helpers/label_converter.cpp
    wx_test.cpp
)

PEERDIR(
    catboost/libs/data_util
    catboost/libs/logging
    catboost/libs/options
    library/binsaver
    library/containers/2d_array
    library/digest/md5
    library/malloc/api
    library/threading/local_executor
    contrib/libs/clapack
)

GENERATE_ENUM_SERIALIZATION(eval_helpers.h)

END()
