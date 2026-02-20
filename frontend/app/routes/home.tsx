import { Typography, Container } from '@mui/material';
import HeroPanel from '../components/HeroPanel';
import InfoCards from '../components/InfoCards';
import ProjectCards from '../components/ProjectCards';
import EducationSection from '../components/EducationSection';
import SkillPills from '../components/SkillPills';

export default function Home() {
  return (
    <>
      <HeroPanel />
      <InfoCards />
      <Container maxWidth="lg" sx={{ mb: 4 }}>
        <Typography sx={{ color: 'text.secondary', lineHeight: 1.8 }}>
          Apart from working on machine learning and software engineering projects, I also enjoy learning more about mathematics, including graph theory and complex analysis. Mathematics is particularly enjoyable due to its rich structure and the beauty of certainty of truth. I also enjoy learning about history, in particular early Christian religious practice following Jesus, as well as early Judaism. Outside academic interests, I engage in biking, watching sports and cooking.
          <br /> <br />
          Feel free to check out my notes, available at the link in the header.
        </Typography>
      </Container>
      <ProjectCards />
      <EducationSection />
      <SkillPills />
    </>
  );
}
