#!/bin/bash
# Gamesdk build driver script
# Usage:
# ./build.sh
#   Builds the gamesdk with Swappy, Tuning Fork and Oboe (no samples)
# ./build.sh samples
#   Builds the gamesdk with Swappy and the Swappy samples
# ./build.sh full
#   Builds the gamesdk with Swappy, Tuning Fork, Oboe and all samples

set -e # Exit on error

# Set up the environment
export ANDROID_HOME=$(pwd)/../prebuilts/sdk
export ANDROID_NDK_HOME=$(pwd)/../prebuilts/ndk/r20
export BUILDBOT_SCRIPT=true
export BUILDBOT_CMAKE=$(pwd)/../prebuilts/cmake/linux-x86
cp -Rf samples/sdk_licenses ../prebuilts/sdk/licenses

# Use the distribution path given to the script by the build bot in DIST_DIR. Otherwise,
# build in the default location.
if [[ -z $DIST_DIR ]]
then
    dist_dir=$(pwd)/../dist
else
    dist_dir=$DIST_DIR
fi

# Build the Game SDK distribution zip and the zips for Maven AARs
if [[ $1 == "full" ]]
then
    package_name=fullsdk
    ./gradlew packageZip -Plibraries=swappy,tuningfork,oboe,game_activity,game_text_input,paddleboat -PincludeSampleSources -PincludeSampleArtifacts -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=swappy -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=tuningfork -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=oboe -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=game_activity -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=game_text_input -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=paddleboat -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew jetpadJson -Plibraries=swappy,tuningfork,game_activity,game_text_input,paddleboat -PdistPath="$dist_dir" -PpackageName=$package_name
elif [[ $1 == "samples" ]]
then
    package_name=gamesdk
    ./gradlew packageZip -Plibraries=swappy -PincludeSampleSources -PincludeSampleArtifacts -PdistPath="$dist_dir"
    ./gradlew packageMavenZip -Plibraries=swappy -PdistPath="$dist_dir"
else
    package_name=gamesdk
    ./gradlew packageZip -Plibraries=swappy,tuningfork,oboe,game_activity,game_text_input,paddleboat -PincludeSampleSources -PdistPath="$dist_dir"
    ./gradlew packageMavenZip -Plibraries=swappy -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=tuningfork -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=oboe -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=game_activity -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=game_text_input -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew packageMavenZip -Plibraries=paddleboat -PdistPath="$dist_dir" -PpackageName=$package_name
    ./gradlew jetpadJson -Plibraries=swappy,tuningfork,game_activity,game_text_input,paddleboat -PdistPath="$dist_dir" -PpackageName=$package_name
fi

# Calculate hash of the zip file
pushd "$dist_dir/$package_name"
sha256sum gamesdk.zip > gamesdk.zip.sha256
popd

pushd "$dist_dir/$package_name"
# Remove intermediate files that would be very costly to store
rm -rf libs prefab
# Remove other files that we don't care about and are polluting the output
rm -rf external third_party src include samples aar
popd
