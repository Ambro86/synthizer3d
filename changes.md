# 0.12.6 (2023-05-20)

- Bind the sine generators
- Start building Python 3.11 wheels
- Update Synthizer so that setting positions or orientations to values
  containing NaN will error.

# 0.12.4 (2022-09-24)

- Upgrade Synthizer to 0.11.9.

# 0.12.3 (2022-03-22)

- More typing fixes.  This should work now.

# 0.12.2 (2022-03-14)

- Add py.typed.
- Start building wheels for 3.7 and up again.

# 0.12.1 (2021-12-04)

- Build Python 3.10 Windows wheels.
- Drop all Windows wheels older than 3.9.  If you need these, `pip install` with
  properly configured C compilers will stil work.

  # 0.12.0 (2021-11-28)

- Support automation.
- All properties now need to use `.value = ` etc instead of just the raw
  property access.
- Extract the manual from Synthizer's repo and improve it; currently there is no
  GitHub pages, so you'll need to look in the repository itself.
