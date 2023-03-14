PYTHON = python3


install: venv
	@${PYTHON} -m setup build_ext --inplace
	@cd docs/ && $(MAKE) html


clean:
	@find pdcast/ -name "*.c" -type f -delete
	@find pdcast/ -name "*.so" -type f -delete
	@rm -r docs/build
