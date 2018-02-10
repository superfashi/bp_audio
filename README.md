# bp_audio
**bp** stands for **bilibili ported**, but also **bai piao**.

![banner](https://superfashi.b0.upaiyun.com/wp-content/uploads/2018/02/banner-bp-audio.png)

An out-of-the-box extractor for Bilibili's official FLAC music.

It can download Bilibili-purchased music in *SQ* (aka lossless) version without an account whatsoever. While downloading, it can also acquire music's metadata, including album art. It can also download the whole *menu* (aka album) at one time.

# Example
![Example-GIF](https://superfashi.b0.upaiyun.com/wp-content/uploads/2018/02/bp_example.gif)

With metadata included:

![Example-Meta](https://superfashi.b0.upaiyun.com/wp-content/uploads/2018/02/bp_example.png)

Try running this command on your own computer.

# Distribution
You can download the pre-compiled *x86_64 (amd64)* version [here](https://github.com/hanbang-wang/bp_audio/releases), which should be running without any problem on latest Windows devices. Requests for other versions will likely be ignored due to the reason in [License & Disclaimer](#license--disclaimer) section, so you will have to compile it by yourself: see [Compilation](#compilation) section below.

# Compilation
First you'll need *[Qt framework](https://www.qt.io)*; *Qt Creator* is highly suggested.

Next you'll need:

- [gumbo-query](https://github.com/lazytiger/gumbo-query) for HTML DOM parsing.

  >   Actually I can use regex, but that just makes me an idiot.

- [TagLib](https://github.com/taglib/taglib) for audio file metadata.

Then just `qmake bp_audio.pro` and `make`, as what you would do for every Qt project.

# Usage
```bash
Usage: bp_audio
Get to know some free official FLAC music.

Options:
  -a, --au <au>     Audio AU number.
  --menu            Download whole menu.
  -o, --out <path>  Output folder path.
  -?, -h, --help    Displays this help.
  -v, --version     Displays version information.
```

# License & Disclaimer
This little repo is unlicensed, meaning it's released into public domain and you can do anything you want. For more information, please refer to [http://unlicense.org](http://unlicense.org).

This is only a proof-of-concept of how bad Bilibili's inner software and web developers are—the mixed use of params in `path` and `query`, the **useless** key validation of CDN, etc. Therefore, it shall only be used for research and educational purpose.

Neither this project, nor its creator(s), is responsible for any misuses, illegal behaviors, and so forth.