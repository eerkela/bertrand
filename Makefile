PYTHON = python3
HEADERS ?= false
HEADER_PATH ?= ~/.local/include
MAJOR ?= false
MINOR ?= false
PATCH ?= false


help:
	@echo "----------------------------------------------------------------------------"
	@echo "make install [HEADERS=false, HEADER_PATH=~/.local/include]"
	@echo "    install locally with optimizations.  Optional C++ headers are installed to HEADER_PATH"
	@echo
	@echo "make editable [HEADERS=false, HEADER_PATH=~/.local/include]"
	@echo "    install locally in editable mode"
	@echo
	@echo "make debug [HEADERS=false, HEADER_PATH=~/.local/include]"
	@echo "    install locally with debug symbols and logging enabled"
	@echo
	@echo "make release [MAJOR=false, MINOR=false, PATCH=false]"
	@echo "    build wheels and publish to PyPI (requires one of MAJOR, MINOR, PATCH=true)"
	@echo
	@echo "make clean [HEADER_PATH=~/.local/include]"
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
	HEADERS=$(HEADERS) HEADER_PATH=$(HEADER_PATH) pip install .


editable:
	HEADERS=$(HEADERS) HEADER_PATH=$(HEADER_PATH) pip install -e .[dev]


debug:
	DEBUG=1 HEADERS=$(HEADERS) HEADER_PATH=$(HEADER_PATH) pip install -e .[dev]


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

#   remove C++ headers from include directory
	@rm -f $(HEADER_PATH)/bertrand.h
	@rm -rf $(HEADER_PATH)/bertrand

#	remove compiled cython artifacts installed in-place
	@find bertrand/ -name "*.c" -type f -delete
	@find bertrand/ -name "*.so" -type f -delete
	@rm -rf build/

#	remove __pycache__, egg-info, .pytest_cache
	@find . -type d -name "__pycache__" -exec rm -rf {} +
	@find . -type f -name "*.py[co]" -delete
	@find . -type d -name "*.egg-info" -exec rm -rf {} +
	@find . -type d -name ".pytest_cache" -exec rm -rf {} +

#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated


doc:
#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated

# 	rebuild
	@cd docs/ && $(MAKE) html

test:
#	@pytest tests/ --doctest-modules --cov=bertrand --cov-report=term-missing


doctest:
#	validate URL links in documentation
#	@cd docs/ && $(MAKE) linkcheck

#	run doctests
	@sphinx-build -b doctest -q docs/source/ docs/build/
#	@pytest --doctest-glob="*.rst" README.rst docs/

