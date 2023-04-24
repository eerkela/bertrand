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
	@rm -r docs/build
	@rm -r docs/source/generated

# 	rebuild
	@cd docs/ && $(MAKE) html


clean:
#	TODO: replace find with python3 invocations.  Can use pathlib.
#	remove compiled cython elements
	@find pdcast/ -name "*.c" -type f -delete
	@find pdcast/ -name "*.so" -type f -delete
	@rm -r build/

#	clear __pycache__
	@find . | grep -E '(/__pycache__$|\.pyc$|\.pyo$)' | xargs rm -rf

#	remove documentation stubs
	@rm -r docs/build
	@rm -r docs/source/generated


test:
#	check links in documentation
	@cd docs/ && $(MAKE) linkcheck

#	run doctests
	@cd docs/ && $(MAKE) doctest
#	@pytest --doctest-glob="*.rst" README.rst docs/

#	TODO: run pytest suite
#	@pytest tests/
