add_files(
    touch.cpp
    touch.h
    touch_absolute.cpp
    touch_absolute.h
    CONDITION ANDROID AND NOT OPTION_DEDICATED
)
