import qbs

CppApplication {
    name: (qbs.architecture == "x86_64" ? "launcher64" : "launcher")
    type: "application" // To suppress bundle generation on Mac
    consoleApplication: true
    files: [
        "launcher.cpp"
    ]
    Group {
        condition: qbs.architecture == "x86_64"
        cpp.cxxFlags: [
            qbs.buildVariant == "debug" ? "/MTd" : "/MT"
        ]
    }
    cpp.linkerFlags: [
        "-static",
        "-static-libgcc",
        "-static-libstdc++"
    ]
    Group {
        qbs.install: true
        qbs.installPrefix: "dist/"
        qbs.installDir: qbs.buildVariant
        fileTagsFilter: "application"
    }
}
