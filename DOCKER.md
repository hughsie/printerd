Docker container
================

You can build and run printerd inside a docker container.

Here is to build it:

    $ docker build -t printerd --rm ./docker

To run it:

    $ docker run -d -p 631:631/tcp printerd

Doing this is only of limited use for the moment as the IPP interface
is largely unimplemented.
