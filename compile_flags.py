Import("env")

cc = env["CCFLAGS"]
cc += ["-fno-exceptions"]
env.Replace(CCFLAGS=cc)

c = env["CFLAGS"]
c += ["-std=gnu23"]
env.Replace(CFLAGS=c)

cxx = env["CXXFLAGS"]
cxx += ["-std=c++26", "-fno-rtti"]
env.Replace(CXXFLAGS=cxx)
