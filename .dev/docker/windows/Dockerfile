# escape=`

FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Use "--build-arg platform=x64" for 64 bit or x86 for 32 bit.
ARG PLATFORM

# Use to set specific commit to checkout
ARG VCPKGCOMMIT

# Download channel for fixed install.
ARG CHANNEL_BASE_URL=https://aka.ms/vs/16/release

ADD $CHANNEL_BASE_URL/channel C:\TEMP\VisualStudio.chman

SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Download and install Build Tools for Visual Studio 2019 for native desktop
RUN wget $Env:CHANNEL_BASE_URL/vs_buildtools.exe -OutFile 'C:\TEMP\vs_buildtools.exe'; `
    Start-Process -FilePath C:\TEMP\vs_buildtools.exe -ArgumentList `
    "--quiet",                                                      `
    "--norestart",                                                  `
    "--nocache",                                                    `
    "--installPath",                                                `
    "C:\BuildTools",                                                `
    "--wait",                                                       `
    "--channelUri",                                                 `
    "C:\TEMP\VisualStudio.chman",                                   `
    "--installChannelUri",                                          `
    "C:\TEMP\VisualStudio.chman",                                   `
    "--add",                                                        `
    "Microsoft.VisualStudio.Workload.VCTools",                      `
    "Microsoft.Net.Component.4.7.2.SDK",                            `
    "Microsoft.Net.Component.4.7.2.TargetingPack",                  `
    "Microsoft.VisualStudio.Component.VC.ATLMFC",                   `
    "--includeRecommended"                                          `
    -Wait -PassThru;                                                `
    del c:\temp\vs_buildtools.exe;                                  

RUN iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'));   `
    choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System' -y --no-progress; `
    choco install git -y --no-progress

RUN git clone https://github.com/microsoft/vcpkg.git; cd vcpkg; git checkout $Env:VCPKGCOMMIT;

# To explicit set VCPKG to only build Release version of the libraries.
COPY $PLATFORM'-windows-rel.cmake' 'c:\vcpkg\triplets\'$PLATFORM'-windows-rel.cmake'

RUN cd .\vcpkg;                                                     `
    .\bootstrap-vcpkg.bat;                                          `
    .\vcpkg install `
    boost-accumulators `
    boost-asio `
    boost-algorithm `
    boost-filesystem `
    boost-format `
    boost-graph `
    boost-interprocess `
    boost-iostreams `
    boost-math `
    boost-ptr-container `
    boost-signals2 `
    boost-sort `
    boost-uuid `
    boost-cmake `
    flann `
    nanoflann `
    eigen3 `
    qhull `
    glew `
    freeglut `
    vtk[core,qt,opengl] `
    gtest `
    benchmark `
    openni2 `
    cjson `
    --triplet $Env:PLATFORM-windows-rel --host-triplet $Env:PLATFORM-windows-rel --clean-after-build --x-buildtrees-root=C:\b;

