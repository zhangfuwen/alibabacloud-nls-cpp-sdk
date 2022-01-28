# intruction

一个Linux下的基于IBus的,支持五笔拼音混输, 支持单纯拼音输入,支持五笔输入,还支持语音输入的输入法.


Code under pinyin folder and dictionary file res/dict_pinyin.dat come from [Google pinyin IME](https://android.googlesource.com/platform/packages/inputmethods/PinyinIME).



# Build and Install

这是一个cmake工程,所有用正常的cmake编译就行了.

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```

你也可以先构建一个deb包, 然后再安装deb包:

```bash
mkdir build
cd build
cmake ..
make
cpack
sudo dpkg -i audio_ime-1.1-Linux.deb

```

# Snapshots

![配置界面](./doc/1_config.png)
![五笔配置界面](./doc/2_config_wubi.png)
![输入界面](./doc/3_input.png)
![语音输入界面](./doc/4_input_by_speech.png)



