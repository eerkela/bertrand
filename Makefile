PYTHON = python3


help:
	@echo "-------------------------------------------------------"
	@echo "make install - compile binaries and build documentation"
	@echo "make doc     - build documentation only"
	@echo "make clean   - remove compiled elements/autodoc stubs"
	@echo "make test    - run test suite + doctests"
	@echo "-------------------------------------------------------"


install:
# 	build and compile cython elements
	@${PYTHON} setup.py build_ext --inplace

# 	build documentation
	@cd docs/ && $(MAKE) html


doc:
#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated

# 	rebuild
	@cd docs/ && $(MAKE) html


clean:
#	remove compiled cython elements
	@find pdcast/ -name "*.c" -type f -delete
	@find pdcast/ -name "*.so" -type f -delete
	@rm -rf build/

#	remove egg-info
	@find . -type d -name "*.egg-info" -exec rm -rf {} +

#	clear __pycache__
	@find . -type d -name "__pycache__" -exec rm -rf {} +
	@find . -type f -name "*.py[co]" -delete

#   clear .pytest_cache
	@find . -type d -name ".pytest_cache" -exec rm -rf {} +

#	remove documentation stubs
	@rm -rf docs/build
	@rm -rf docs/source/generated


test:
#	check links in documentation
	@cd docs/ && $(MAKE) linkcheck

#	run doctests
	@cd docs/ && $(MAKE) doctest
#	@pytest --doctest-glob="*.rst" README.rst docs/

#	TODO: run pytest suite
#	@pytest tests/
