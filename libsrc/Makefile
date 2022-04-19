core:
	g++ --shared core/*.cpp --std=c++17 -fPIC -Wl,-z,defs -ldl -lpthread

general:
	g++ --shared general/*.cpp --std=c++17 -fPIC -Iinclude -I. -Wl,-z,defs -ldl -lpthread

gprim:
	g++ --shared gprim/*.cpp --std=c++17 -fPIC -Iinclude -I. -Wl,-z,defs -ldl -lpthread

linalg:
	g++ --shared linalg/*.cpp --std=c++17 -fPIC -Iinclude -I. -Wl,-z,defs -ldl -lpthread

meshing:
	g++ --shared meshing/*.cpp --std=c++17 -fPIC -Iinclude -I. -Wl,-z,defs -ldl -lpthread -Wfatal-errors -Wall






# debug:
# 	# https://stackoverflow.com/questions/1617286/easy-check-for-unresolved-symbols-in-shared-libraries
# 	g++ --shared core/*.cpp general/*.cpp csg/*.cpp geom2d/*.cpp gprim/*.cpp linalg/*.cpp meshing/*.cpp stlgeom/*.cpp --std=c++17 -Iinclude -I. -I/usr/include/tcl -Imeshing -fPIC -Wl,-z,defs
# 	# strategie: die Dateien weglassen, in denen Symbole vorkommen, die noch undefiniert sind


.PHONY:	core general gprim linalg meshing
