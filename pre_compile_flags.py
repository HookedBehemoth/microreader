Import("env")

cc = env["CCFLAGS"]
cc += ["-ffunction-sections", "-fdata-sections", "-fomit-frame-pointer", "-fno-exceptions", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-fno-stack-protector"]
env.Replace(CCFLAGS=cc)

c = env["CFLAGS"]
c += ["-std=gnu23"]
env.Replace(CFLAGS=c)

cxx = env["CXXFLAGS"]
cxx += ["-std=c++26", "-fno-rtti", "-fno-use-cxa-atexit"]
env.Replace(CXXFLAGS=cxx)

# Strip .eh_frame sections from final binary
env.Append(LINKFLAGS=["-Wl,--gc-sections", "-Wl,--strip-debug"])
