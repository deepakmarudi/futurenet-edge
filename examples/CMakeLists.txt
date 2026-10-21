build_lib_example(
    NAME example-basic
    SOURCE_FILES example-basic.cc
    LIBRARIES_TO_LINK
        ${libfuturenet-edge}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libapplications}
        ${libpoint-to-point}
        ${libflow-monitor}
)

build_lib_example(
    NAME example-multiedge
    SOURCE_FILES example-multiedge.cc
    LIBRARIES_TO_LINK
        ${libfuturenet-edge}
        ${libcore}
        ${libnetwork}
        ${libinternet}
        ${libapplications}
        ${libmobility}
        ${libpoint-to-point}
        ${libflow-monitor}
)
