PYTHON = python3
DEBUG ?= false
MAJOR ?= false
MINOR ?= false
PATCH ?= false

.PHONY: test


help:
	@echo "----------------------------------------------------------------------------"
	@echo "make install [DEBUG=false]"
	@echo "    install locally in user site-packages"
	@echo
	@echo "make editable [DEBUG=false]"
	@echo "    install locally in editable mode"
	@echo
	@echo "make extensions [DEBUG=false]"
	@echo "    force recompilation of all C++ extensions in-place"
	@echo
	@echo "make release [MAJOR=false, MINOR=false, PATCH=false]"
	@echo "    build wheels and publish to PyPI (requires one of MAJOR, MINOR, PATCH=true)"
	@echo
	@echo "make clean"
	@echo "    uninstall and remove all build files, including C++ headers"
	@echo
	@echo "make doc"
	@echo "    build sphinx HTML documentation"
	@echo
	@echo "make test"
	@echo "    run pytest suite"
	@echo
	@echo "make doctest"
	@echo "    run doctests in sphinx documentation"
	@echo "----------------------------------------------------------------------------"


install:
	DEBUG=$(DEBUG) pip install .


editable:
	DEBUG=$(DEBUG) pip install -e .[dev]


extensions:
	DEBUG=$(DEBUG) python setup.py build_ext --inplace --force


release:
	@if [ "$(MAJOR)" = "true" ]; then \
        echo "Releasing major version"; \
		bumpver update --major; \
	elif [ "$(MINOR)" = "true" ]; then \
		echo "Releasing minor version"; \
		bumpver update --minor; \
	elif [ "$(PATCH)" = "true" ]; then \
		echo "Releasing patch version"; \
		bumpver update --patch; \
	else \
		echo "Error: release builds must specify one of MAJOR, MINOR, or PATCH."; \
		echo "Usage: make release MAJOR=true"; \
		echo "       make release MINOR=true"; \
		echo "       make release PATCH=true"; \
		exit 1; \
	fi


clean:
#   uninstall python package
	@pip uninstall -y bertrand

#   remove build artifacts
	@rm -rf build/
	@rm -rf test/build/
	@rm test/.compile_flags
	@rm -rf third_party/pcre2-10.43/
	@rm -rf third_party/googletest-1.14.0/

#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated

#	remove __pycache__, egg-info, .pytest_cache
	@find . -type d -name "__pycache__" -exec rm -rf {} +
	@find . -type f -name "*.py[co]" -delete
	@find . -type d -name "*.egg-info" -exec rm -rf {} +
	@find . -type d -name ".pytest_cache" -exec rm -rf {} +


doc:
#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated

# 	rebuild
	@cd docs/ && $(MAKE) html


test:
#	recompile C++ tests using CMake, then invoke pytest in root directory to run both
#   C++ and Python tests using pytest-cpp
	@{ \
	set -e; \
	mkdir -p test/build; \
	cd test/build; \
	cmake .. -DCMAKE_CXX_FLAGS=-fPIC > /dev/null; \
	make -s -j8; \
	cd ../..; \
	pytest; \
	}


doctest:
#	validate URL links in documentation
#	@cd docs/ && $(MAKE) linkcheck

#	run doctests
	@sphinx-build -b doctest -q docs/source/ docs/build/
#	@pytest --doctest-glob="*.rst" README.rst docs/
